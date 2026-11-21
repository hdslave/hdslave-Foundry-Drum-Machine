// Loads Foundry.clap as a host does and checks it: description, ports, note names and the drum map Melodee reads;
// every parameter's range, text and default; state; presets; each pad's voice in each model (sounding, bounded,
// starting on its frame); velocity, accent, choke groups, the host's choke; Prob, Cycle, Flam, Sub-Step, Nudge,
// Link, Humanize; the Keys voice's pitch; the GM key map; the side chain; the effects; the same output whatever the
// block size; finite, bounded output at random and extreme settings, at 44.1, 48 and 96 kHz. Prints each voice's
// level and length in each model, and writes demo renders as WAV files into the output folder.
//   host Foundry.clap OUT_FOLDER
#include "wav.h"

#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace
{
int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_MSG(c, ...) do { ++checks; if (! (c)) { ++failures; std::printf ("FAIL %s:%d  %s: ", __FILE__, __LINE__, #c); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

const clap_plugin_entry* entry = nullptr;
const clap_plugin_factory* factory = nullptr;

int rescans = 0, presetsLoaded = 0, presetErrors = 0;
const void* hostExtension (const clap_host*, const char* id)
{
    static const clap_host_log log { [] (const clap_host*, clap_log_severity s, const char* m) { std::printf ("  [plug-in log %d] %s\n", (int) s, m); } };
    static const clap_host_params params { [] (const clap_host*, clap_param_rescan_flags f) { rescans += (f & CLAP_PARAM_RESCAN_VALUES) != 0; },
                                           [] (const clap_host*, clap_id, clap_param_clear_flags) {}, [] (const clap_host*) {} };
    static const clap_host_preset_load presetLoad {
        [] (const clap_host*, uint32_t, const char*, const char*, int32_t, const char*) { ++presetErrors; },
        [] (const clap_host*, uint32_t, const char*, const char*) { ++presetsLoaded; },
    };
    if (std::strcmp (id, CLAP_EXT_LOG) == 0) return &log;
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0) return &presetLoad;
    return nullptr;
}

clap_host host {
    CLAP_VERSION, nullptr, "Foundry test host", "tests", "", "1",
    hostExtension, [] (const clap_host*) {}, [] (const clap_host*) {}, [] (const clap_host*) {},
};

// Input events for one process call, in time order.
struct Events
{
    struct Any { union { clap_event_header header; clap_event_param_value param; clap_event_note note; clap_event_midi midi; }; };
    std::vector<Any> list;
    clap_input_events in { this, [] (const clap_input_events* e) { return (uint32_t) static_cast<const Events*> (e->ctx)->list.size(); },
                           [] (const clap_input_events* e, uint32_t i) { return &static_cast<const Events*> (e->ctx)->list[i].header; } };
    void param (uint32_t time, clap_id id, double value)
    {
        Any a {};
        a.param = { { sizeof (clap_event_param_value), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 }, id, nullptr, -1, -1, -1, -1, value };
        list.push_back (a);
    }
    void note (uint32_t time, uint16_t type, int16_t key, double velocity)
    {
        Any a {};
        a.note = { { sizeof (clap_event_note), time, CLAP_CORE_EVENT_SPACE_ID, type, 0 }, -1, 0, 0, key, velocity };
        list.push_back (a);
    }
    void midi (uint32_t time, uint8_t status, uint8_t d1, uint8_t d2)
    {
        Any a {};
        a.midi = { { sizeof (clap_event_midi), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 }, 0, { status, d1, d2 } };
        list.push_back (a);
    }
    void sort() { std::stable_sort (list.begin(), list.end(), [] (const Any& a, const Any& b) { return a.header.time < b.header.time; }); }
};

struct Transport
{
    bool playing = false;
    double bpm = 120.0, beats = 0.0;
};

// A note at a frame.
struct Note
{
    uint64_t frame;
    int key;
    double velocity = 0.8;
};

struct Instance
{
    const clap_plugin* plugin = nullptr;
    const clap_plugin_params* params = nullptr;
    const clap_plugin_state* state = nullptr;
    const clap_plugin_preset_load* presets = nullptr;
    std::map<std::string, clap_id> ids;
    double rate = 48000.0;
    std::vector<float> keyL, keyR; // the SC Key port, from the last render

    explicit Instance (double sampleRate = 48000.0, uint32_t maxFrames = 4096) : rate (sampleRate)
    {
        const auto* d = factory->get_plugin_descriptor (factory, 0);
        plugin = factory->create_plugin (factory, &host, d->id);
        CHECK (plugin != nullptr && plugin->init (plugin));
        params = static_cast<const clap_plugin_params*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
        presets = static_cast<const clap_plugin_preset_load*> (plugin->get_extension (plugin, CLAP_EXT_PRESET_LOAD));
        for (uint32_t i = 0; params && i < params->count (plugin); ++i)
        {
            clap_param_info info {};
            params->get_info (plugin, i, &info);
            ids[info.name] = info.id;
        }
        CHECK (plugin->activate (plugin, rate, 1, maxFrames));
        CHECK (plugin->start_processing (plugin));
    }
    ~Instance()
    {
        plugin->stop_processing (plugin);
        plugin->deactivate (plugin);
        plugin->destroy (plugin);
    }
    clap_id id (const std::string& name)
    {
        const auto it = ids.find (name);
        if (it == ids.end())
        {
            std::printf ("FAIL no parameter named \"%s\"\n", name.c_str());
            ++failures;
            return CLAP_INVALID_ID;
        }
        return it->second;
    }
    bool loadPreset (const char* key) { return presets != nullptr && presets->from_location (plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, key); }
    double value (const std::string& name)
    {
        double v = 0.0;
        params->get_value (plugin, id (name), &v);
        return v;
    }
    double valueOf (const std::string& name, const std::string& text)
    {
        double v = 0.0;
        CHECK (params->text_to_value (plugin, id (name), text.c_str(), &v));
        return v;
    }

    // Renders `frames` in blocks of `block`; `events(frame, n, list)` adds events for the block starting at frame.
    void render (uint64_t frames, uint32_t block, std::vector<float>& left, std::vector<float>& right,
                 const std::function<Transport (uint64_t)>& transport, const std::function<void (uint64_t, uint32_t, Events&)>& events = {})
    {
        left.assign (frames, 0.0f);
        right.assign (frames, 0.0f);
        keyL.assign (frames, 0.0f);
        keyR.assign (frames, 0.0f);
        clap_output_events out { nullptr, [] (const clap_output_events*, const clap_event_header*) { return true; } };
        for (uint64_t at = 0; at < frames; at += block)
        {
            const uint32_t n = (uint32_t) std::min<uint64_t> (block, frames - at);
            const auto t = transport (at);
            clap_event_transport tr {};
            tr.header = { sizeof (clap_event_transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
            tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | (t.beats >= 0.0 ? CLAP_TRANSPORT_HAS_BEATS_TIMELINE : 0)
                     | (t.playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
            tr.tempo = t.bpm;
            tr.song_pos_beats = (clap_beattime) std::llround (t.beats * (double) CLAP_BEATTIME_FACTOR);
            tr.tsig_num = 4, tr.tsig_denom = 4;
            Events e;
            if (events)
                events (at, n, e);
            e.sort();
            float* outs[2] = { left.data() + at, right.data() + at };
            float* keys[2] = { keyL.data() + at, keyR.data() + at };
            clap_audio_buffer buffers[2] {};
            buffers[0].data32 = outs;
            buffers[0].channel_count = 2;
            buffers[1].data32 = keys;
            buffers[1].channel_count = 2;
            clap_process p {};
            p.steady_time = (int64_t) at;
            p.frames_count = n;
            p.transport = &tr;
            p.audio_outputs = buffers;
            p.audio_outputs_count = 2;
            p.in_events = &e.in;
            p.out_events = &out;
            CHECK (plugin->process (plugin, &p) != CLAP_PROCESS_ERROR);
        }
    }
    // Parameter changes before anything plays: one silent, stopped block of one frame.
    void setup (const std::vector<std::pair<std::string, double>>& values)
    {
        std::vector<float> l, r;
        render (1, 1, l, r, [] (uint64_t) { return Transport {}; }, [&] (uint64_t, uint32_t, Events& e) {
            for (auto& [name, v] : values)
                e.param (0, id (name), v);
        });
    }
    // Notes into a render, the transport stopped or playing from the top at `bpm`.
    void play (uint64_t frames, const std::vector<Note>& notes, std::vector<float>& l, std::vector<float>& r, bool playing = false,
               uint32_t block = 128, double bpm = 120.0)
    {
        const double perFrame = bpm / 60.0 / rate;
        render (frames, block, l, r, [&] (uint64_t at) { return Transport { playing, bpm, playing ? (double) at * perFrame : 0.0 }; },
                [&] (uint64_t at, uint32_t n, Events& e) {
                    for (const auto& nt : notes)
                        if (nt.frame >= at && nt.frame < at + n)
                            e.note ((uint32_t) (nt.frame - at), CLAP_EVENT_NOTE_ON, (int16_t) nt.key, nt.velocity);
                });
    }
};

// --- Measures --------------------------------------------------------------------------------------------------
double peakOf (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    double p = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        p = std::max (p, (double) std::fabs (x[i]));
    return p;
}
double energyOf (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    double e = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        e += (double) x[i] * x[i];
    return e;
}
// The first frame louder than `threshold` at or after `from`, -1 none.
long onsetOf (const std::vector<float>& x, size_t from = 0, double threshold = 1e-4)
{
    for (size_t i = from; i < x.size(); ++i)
        if (std::fabs (x[i]) > threshold)
            return (long) i;
    return -1;
}
// The frames where the sound starts again after at least `gap` frames below `threshold`.
std::vector<long> onsetsOf (const std::vector<float>& x, double threshold, size_t gap)
{
    std::vector<long> found;
    size_t quiet = gap;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (std::fabs (x[i]) > threshold)
        {
            if (quiet >= gap)
                found.push_back ((long) i);
            quiet = 0;
        }
        else
            ++quiet;
    }
    return found;
}
// How long until the sound stays 60 dB below its peak, seconds.
double lengthOf (const std::vector<float>& x, double rate)
{
    const double p = peakOf (x);
    if (p <= 0.0)
        return 0.0;
    long last = 0;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::fabs (x[i]) > p * 1e-3)
            last = (long) i;
    return (double) last / rate;
}
// The rate the signal crosses zero upward over [from, to): a pitch estimate for a ringing tone.
double crossingsHz (const std::vector<float>& x, size_t from, size_t to, double rate)
{
    int n = 0;
    long first = -1, last = -1;
    for (size_t i = from + 1; i < std::min (to, x.size()); ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            if (first < 0) first = (long) i;
            last = (long) i;
            ++n;
        }
    return n > 1 ? (n - 1) * rate / (double) (last - first) : 0.0;
}
bool finiteBounded (const std::vector<float>& x, double bound)
{
    for (float s : x)
        if (! std::isfinite (s) || std::fabs (s) > bound)
            return false;
    return true;
}
bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

const char* const tags[] = { "BD", "RS", "SD", "CP", "LT", "MT", "HT", "CH", "OH", "CY", "RD", "CB", "CL", "MA", "CG", "B2" };
const char* const models[] = { "808", "909", "606" };

// Every voice silent but `keep` (its level), the effects dry.
// The pad (note, Key Map Pads) a voice plays on: from A01 (note 36, bottom left) BD, SD, CP, CH; B2, RS, CY, OH;
// LT, MT, HT, CB; RD, CL, MA, CG. By voice: BD RS SD CP LT MT HT CH OH CY RD CB CL MA CG B2.
constexpr int padOfVoice[16] = { 0, 5, 1, 2, 8, 9, 10, 3, 7, 6, 12, 11, 13, 14, 15, 4 };
constexpr int padKey (int voice) { return 36 + padOfVoice[voice]; }

std::vector<std::pair<std::string, double>> solo (int keep)
{
    std::vector<std::pair<std::string, double>> v;
    for (int i = 0; i < 16; ++i)
        if (i != keep)
            v.push_back ({ std::string (tags[i]) + " Level", -60.0 });
    return v;
}

// --- The checks ----------------------------------------------------------------------------------------------------

void testDescription()
{
    CHECK (factory->get_plugin_count (factory) == 1);
    const auto* d = factory->get_plugin_descriptor (factory, 0);
    CHECK (d != nullptr && std::strcmp (d->id, "com.hdslave.foundry") == 0);
    CHECK (d != nullptr && std::strcmp (d->name, "hdslave Foundry") == 0);
    bool instrument = false, drums = false;
    for (const char* const* f = d->features; *f; ++f)
        instrument |= std::strcmp (*f, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0, drums |= std::strcmp (*f, CLAP_PLUGIN_FEATURE_DRUM_MACHINE) == 0;
    CHECK (instrument && drums);

    Instance i;
    const auto* audio = static_cast<const clap_plugin_audio_ports*> (i.plugin->get_extension (i.plugin, CLAP_EXT_AUDIO_PORTS));
    CHECK (audio && audio->count (i.plugin, true) == 0 && audio->count (i.plugin, false) == 2);
    clap_audio_port_info port {};
    CHECK (audio && audio->get (i.plugin, 0, false, &port) && port.channel_count == 2 && (port.flags & CLAP_AUDIO_PORT_IS_MAIN));
    CHECK (audio && audio->get (i.plugin, 1, false, &port) && port.channel_count == 2 && std::strcmp (port.name, "SC Key") == 0
           && ! (port.flags & CLAP_AUDIO_PORT_IS_MAIN));
    const auto* notes = static_cast<const clap_plugin_note_ports*> (i.plugin->get_extension (i.plugin, CLAP_EXT_NOTE_PORTS));
    clap_note_port_info np {};
    CHECK (notes && notes->count (i.plugin, true) == 1 && notes->get (i.plugin, 0, true, &np));
    CHECK ((np.supported_dialects & CLAP_NOTE_DIALECT_MIDI) && (np.supported_dialects & CLAP_NOTE_DIALECT_CLAP));
    const auto* names = static_cast<const clap_plugin_note_name*> (i.plugin->get_extension (i.plugin, CLAP_EXT_NOTE_NAME));
    CHECK (names && names->count (i.plugin) == 16);
    clap_note_name nn {};
    // Pads A01-A16 from the bottom left, along each row and up.
    static const char* const padNames[16] = { "Bass Drum", "Snare Drum", "Hand Clap", "Closed Hat", "Bass Drum 2", "Rim Shot",
                                              "Crash", "Open Hat", "Low Tom", "Mid Tom", "High Tom", "Cowbell", "Ride",
                                              "Claves", "Maracas", "Conga" };
    for (uint32_t k = 0; k < 16; ++k)
        CHECK_MSG (names && names->get (i.plugin, k, &nn) && nn.key == 36 + (int) k && std::strcmp (nn.name, padNames[k]) == 0, "pad %u", k);
}

// Melodee finds a drum machine's pads by a JSON map in its library: "pad_layout":"drums" and a name and a note for
// each voice. It must be there once, with the sixteen pads on notes 36-51.
void testDrumMap (const char* path)
{
    std::ifstream f (path, std::ios::binary);
    const std::string bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    const size_t at = bytes.find ("{\"pad_layout\":\"drums\"");
    CHECK (at != std::string::npos);
    CHECK (bytes.find ("\"pad_layout\"", at + 10) == std::string::npos);
    if (at == std::string::npos)
        return;
    const std::string map = bytes.substr (at, bytes.find ('\0', at) - at);
    int notes = 0;
    for (int k = 36; k <= 51; ++k)
        notes += map.find ("\"note\":" + std::to_string (k) + "}") != std::string::npos;
    CHECK (notes == 16);
    // Each pad named as the note-name extension names it: A01-A16 from the bottom left, along each row and up.
    static const char* const padNames[16] = { "Bass Drum", "Snare Drum", "Hand Clap", "Closed Hat", "Bass Drum 2", "Rim Shot",
                                              "Crash", "Open Hat", "Low Tom", "Mid Tom", "High Tom", "Cowbell", "Ride",
                                              "Claves", "Maracas", "Conga" };
    for (int k = 0; k < 16; ++k)
        CHECK_MSG (map.find ("\"name\":\"" + std::string (padNames[k]) + "\",\"note\":" + std::to_string (36 + k) + "}") != std::string::npos, "pad %d", k);
    CHECK (map.back() == '}');
}

void testParameters()
{
    Instance i;
    const uint32_t n = i.params->count (i.plugin);
    CHECK (n > 600);
    std::map<clap_id, int> idsSeen;
    std::map<std::string, int> namesSeen;
    for (uint32_t k = 0; k < n; ++k)
    {
        clap_param_info info {};
        CHECK (i.params->get_info (i.plugin, k, &info));
        CHECK (idsSeen[info.id]++ == 0);
        CHECK_MSG (namesSeen[info.name]++ == 0, "%s twice", info.name);
        CHECK (info.min_value < info.max_value);
        CHECK (info.default_value >= info.min_value && info.default_value <= info.max_value);
        double v = -1.0;
        CHECK (i.params->get_value (i.plugin, info.id, &v) && v == info.default_value);
        // Text both ways, at the ends, the default and between.
        for (double x : { info.min_value, info.max_value, info.default_value, 0.37 * info.min_value + 0.63 * info.max_value })
        {
            if (info.flags & CLAP_PARAM_IS_STEPPED)
                x = std::round (x);
            char text[128] = {};
            CHECK (i.params->value_to_text (i.plugin, info.id, x, text, sizeof (text)));
            double back = 0.0;
            CHECK_MSG (i.params->text_to_value (i.plugin, info.id, text, &back), "%s: \"%s\"", info.name, text);
            const double tol = (info.max_value - info.min_value) * 0.01 + 1e-6;
            // Texts round (a time to ms, Hz to whole hertz): the value comes back near.
            CHECK_MSG (std::fabs (back - x) <= tol || (std::strcmp (text, "-inf dB") == 0 && back == info.min_value), "%s: %g -> \"%s\" -> %g",
                       info.name, x, text, back);
        }
    }
    // Rows of three for each voice: every voice's block starts with its Level.
    uint32_t rowAt = 0;
    for (uint32_t k = 0; k < n; ++k)
    {
        clap_param_info info {};
        i.params->get_info (i.plugin, k, &info);
        const std::string name = info.name;
        if (name.size() > 6 && name.compare (name.size() - 6, 6, " Level") == 0 && name.find ("Reverb") == std::string::npos
            && name.find ("Delay") == std::string::npos)
        {
            CHECK_MSG (k % 3 == 0, "%s at %u", info.name, k);
            ++rowAt;
        }
    }
    CHECK (rowAt == 16);
    CHECK (i.valueOf ("BD Model", "909") == 1.0);
    CHECK (i.valueOf ("OH Choke", "1") == 1.0);
    CHECK (i.value ("OH Choke") == 1.0 && i.value ("CH Choke") == 1.0);
    CHECK (std::fabs (i.valueOf ("BD Pan", "L50") + 0.5) < 1e-6);
    CHECK (std::fabs (i.valueOf ("Analog Filter", "HP 400 Hz")) > 0.4);
}

// State: values saved and loaded, at random settings; a state with an unknown id still loads.
void testState()
{
    std::vector<uint8_t> saved;
    std::vector<std::pair<clap_id, double>> set;
    {
        Instance i;
        std::mt19937 rng (7);
        std::vector<std::pair<std::string, double>> values;
        for (auto& [name, id] : i.ids)
        {
            clap_param_info info {};
            for (uint32_t k = 0; k < i.params->count (i.plugin); ++k)
            {
                i.params->get_info (i.plugin, k, &info);
                if (info.id == id)
                    break;
            }
            double v = info.min_value + (info.max_value - info.min_value) * std::uniform_real_distribution<double> (0, 1) (rng);
            if (info.flags & CLAP_PARAM_IS_STEPPED)
                v = std::round (v);
            values.push_back ({ name, v });
            set.push_back ({ id, v });
        }
        i.setup (values);
        clap_ostream out { &saved, [] (const clap_ostream* s, const void* d, uint64_t n) -> int64_t {
            auto* v = static_cast<std::vector<uint8_t>*> (s->ctx);
            v->insert (v->end(), (const uint8_t*) d, (const uint8_t*) d + n);
            return (int64_t) n;
        } };
        CHECK (i.state->save (i.plugin, &out));
    }
    Instance j;
    struct Reader { const std::vector<uint8_t>* bytes; size_t at; } reader { &saved, 0 };
    clap_istream in { &reader, [] (const clap_istream* s, void* d, uint64_t n) -> int64_t {
        auto* r = static_cast<Reader*> (s->ctx);
        const uint64_t k = std::min<uint64_t> (n, r->bytes->size() - r->at);
        std::memcpy (d, r->bytes->data() + r->at, k);
        r->at += k;
        return (int64_t) k;
    } };
    CHECK (j.state->load (j.plugin, &in));
    int wrong = 0;
    for (auto& [id, v] : set)
    {
        double got = 0.0;
        j.params->get_value (j.plugin, id, &got);
        wrong += std::fabs (got - v) > 1e-5;
    }
    CHECK (wrong == 0);
    // An unknown id (from a later version) is skipped.
    std::vector<uint8_t> odd = { 'F', 'N', 'D', 'Y', 1, 0, 0, 0, 2, 0, 0, 0 };
    auto pair = [&] (uint32_t id, float v) {
        const uint8_t* a = (const uint8_t*) &id;
        const uint8_t* b = (const uint8_t*) &v;
        odd.insert (odd.end(), a, a + 4);
        odd.insert (odd.end(), b, b + 4);
    };
    pair (60000, 1.0f);
    pair (j.id ("BD Tune"), 5.0f);
    Reader r2 { &odd, 0 };
    clap_istream in2 { &r2, in.read };
    CHECK (j.state->load (j.plugin, &in2));
    CHECK (std::fabs (j.value ("BD Tune") - 5.0) < 1e-6);
    CHECK (j.value ("SD Level") == -2.0); // the rest back to defaults
}

void testPresets()
{
    const auto* discovery = static_cast<const clap_preset_discovery_factory*> (entry->get_factory (CLAP_PRESET_DISCOVERY_FACTORY_ID));
    CHECK (discovery && discovery->count (discovery) == 1);
    struct Found { std::vector<std::pair<std::string, std::string>> presets; int locations = 0; } found;
    clap_preset_discovery_indexer indexer { CLAP_VERSION, "tests", "", "", "1", &found,
        [] (const clap_preset_discovery_indexer*, const clap_preset_discovery_filetype*) { return true; },
        [] (const clap_preset_discovery_indexer* x, const clap_preset_discovery_location*) { ++static_cast<Found*> (x->indexer_data)->locations; return true; },
        [] (const clap_preset_discovery_indexer*, const clap_preset_discovery_soundpack*) { return true; },
        [] (const clap_preset_discovery_indexer*, const char*) -> const void* { return nullptr; } };
    const auto* d = discovery->get_descriptor (discovery, 0);
    const auto* provider = discovery->create (discovery, &indexer, d->id);
    CHECK (provider && provider->init (provider) && found.locations == 1);
    clap_preset_discovery_metadata_receiver receiver { &found,
        [] (const clap_preset_discovery_metadata_receiver*, int32_t, const char*) {},
        [] (const clap_preset_discovery_metadata_receiver* r, const char* name, const char* key) {
            static_cast<Found*> (r->receiver_data)->presets.push_back ({ name, key });
            return true;
        },
        [] (const clap_preset_discovery_metadata_receiver*, const clap_universal_plugin_id*) {},
        [] (const clap_preset_discovery_metadata_receiver*, const char*) {},
        [] (const clap_preset_discovery_metadata_receiver*, uint32_t) {},
        [] (const clap_preset_discovery_metadata_receiver*, const char*) {},
        [] (const clap_preset_discovery_metadata_receiver*, const char*) {},
        [] (const clap_preset_discovery_metadata_receiver*, const clap_timestamp, const clap_timestamp) {},
        [] (const clap_preset_discovery_metadata_receiver*, const char*) {},
        [] (const clap_preset_discovery_metadata_receiver*, const char*, const char*) {} };
    CHECK (provider && provider->get_metadata (provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &receiver));
    CHECK (found.presets.size() == 9);
    if (provider)
        provider->destroy (provider);

    Instance i;
    const int before = rescans, loads = presetsLoaded;
    for (auto& [name, key] : found.presets)
        CHECK_MSG (i.loadPreset (key.c_str()), "%s", key.c_str());
    CHECK (presetsLoaded - loads == (int) found.presets.size() && rescans - before == (int) found.presets.size());
    CHECK (! i.loadPreset ("no-such") && presetErrors == 1);
    CHECK (i.loadPreset ("909-kit") && i.value ("BD Model") == 1.0 && i.value ("CH Model") == 1.0);
    CHECK (i.loadPreset ("606-mods") && i.value ("SD Model") == 2.0 && i.value ("BD Tune") == -3.0 && std::fabs (i.value ("Accent") - 0.8) < 1e-6);
    CHECK (i.loadPreset ("808-bass") && i.value ("Keys Voice") == 1.0);
    CHECK (i.loadPreset ("default") && i.value ("BD Model") == 0.0 && i.value ("SD Model") == 1.0 && i.value ("Keys Voice") == 0.0);
    // Each preset plays: a beat, bounded and not silent.
    for (auto& [name, key] : found.presets)
    {
        Instance k;
        k.loadPreset (key.c_str());
        std::vector<Note> beat;
        for (int s = 0; s < 16; ++s)
        {
            const uint64_t f = (uint64_t) s * 6000;
            if (s % 4 == 0) beat.push_back ({ f, padKey (0), 1.0 });
            if (s % 8 == 4) beat.push_back ({ f, padKey (2), 0.9 });
            beat.push_back ({ f, padKey (s % 4 == 2 ? 8 : 7), 0.7 });
        }
        std::vector<float> l, r;
        k.play (96000 + 48000, beat, l, r, true);
        CHECK_MSG (finiteBounded (l, 1.0001) && finiteBounded (r, 1.0001), "%s", key.c_str());
        CHECK_MSG (peakOf (l) > 0.1, "%s peak %g", key.c_str(), peakOf (l));
    }
}

// Each pad plays its voice in each model: it sounds, starts on its frame, stays bounded, ends. Prints the table.
void testVoices()
{
    std::printf ("  voice  model   peak   rms(50ms)  length\n");
    for (int v = 0; v < 16; ++v)
        for (int model = 0; model < 3; ++model)
        {
            Instance i;
            auto values = solo (v);
            values.push_back ({ std::string (tags[v]) + " Model", model });
            i.setup (values);
            std::vector<float> l, r;
            i.play (48000 * 9, { { 1000, padKey (v), 0.75 } }, l, r);
            const double peak = peakOf (l), rms = std::sqrt (energyOf (l, 1000, 1000 + 2400) / 2400.0), length = lengthOf (l, 48000.0);
            std::printf ("  %-5s  %-5s  %5.3f   %6.4f    %5.2f s\n", tags[v], models[model], peak, rms, length - 1000.0 / 48000.0);
            CHECK_MSG (peak > 0.05 && peak < 1.0, "%s %s peak %g", tags[v], models[model], peak);
            CHECK_MSG (finiteBounded (l, 1.0) && finiteBounded (r, 1.0), "%s %s", tags[v], models[model]);
            const long on = onsetOf (l, 0, 1e-6);
            CHECK_MSG (on >= 1000 && on <= 1002, "%s %s starts at %ld", tags[v], models[model], on);
            CHECK_MSG (peakOf (l, 0, 1000) == 0.0, "%s %s before its note", tags[v], models[model]);
            CHECK_MSG (peakOf (l, l.size() - 4800) == 0.0, "%s %s still sounding after 9 s", tags[v], models[model]);
        }
}

// The power at `hz` over [from, to) (Goertzel), for checking where a voice's partials are.
double powerAt (const std::vector<float>& x, size_t from, size_t to, double hz, double rate)
{
    const double w = 2.0 * M_PI * hz / rate, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
    {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1, s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
// True when `hz` stands above the frequencies a fifth around it: a partial there.
bool partialAt (const std::vector<float>& x, size_t from, size_t to, double hz, double rate)
{
    const double p = powerAt (x, from, to, hz, rate);
    return p > 4.0 * powerAt (x, from, to, hz * 0.75, rate) && p > 4.0 * powerAt (x, from, to, hz * 1.33, rate);
}

// What the hardware documents, held to: see the README's sources.
void testCharacter()
{
    auto alone = [] (int voice, int model, std::vector<std::pair<std::string, double>> extra, double vel = 0.75, uint64_t frames = 48000) {
        Instance i;
        auto values = solo (voice);
        values.push_back ({ std::string (tags[voice]) + " Model", (double) model });
        values.push_back ({ "Clip", 0.0 });
        values.insert (values.end(), extra.begin(), extra.end());
        i.setup (values);
        std::vector<float> l, r;
        i.play (frames, { { 0, padKey (voice), vel } }, l, r);
        return l;
    };
    // 808 kick, as recorded from the machine: a short first lobe (the cosine-phase strike and the raised attack), the
    // sigh (sharper early than late), and about 53 Hz once it settles.
    {
        const auto k = alone (0, 0, { { "BD Decay", 0.7 } });
        const long firstZero = [&] { for (size_t i = 10; i < 4800; ++i) if (k[i - 1] > 0.0f && k[i] <= 0.0f) return (long) i; return -1L; }();
        CHECK_MSG (firstZero > 0.001 * 48000 && firstZero < 0.0025 * 48000, "first lobe ends at %ld", firstZero); // recorded: 1.7 ms
        const double early = crossingsHz (k, 480, 9600, 48000.0), late = crossingsHz (k, 24000, 48000, 48000.0);
        CHECK_MSG (early > late * 1.02 && early < late * 1.3, "sigh %g -> %g Hz", early, late);
        CHECK_MSG (std::fabs (late - 53.0) < 1.0, "settles at %g Hz", late);
        // Louder sighs more (the leakage follows the swing).
        const auto soft = alone (0, 0, { { "BD Decay", 0.7 }, { "BD Vel Sens", 1.0 } }, 0.3);
        CHECK (crossingsHz (soft, 480, 9600, 48000.0) < early);
    }
    // 909 kick (B2's defaults): swept from well above its settled pitch.
    {
        const auto k = alone (15, 1, { { "B2 Decay", 0.7 }, { "B2 Attack", 0.0 } });
        const double start = crossingsHz (k, 0, 1440, 48000.0), settled = crossingsHz (k, 24000, 48000, 48000.0);
        CHECK_MSG (start > 1.4 * settled && std::fabs (settled - 53.0) < 1.5, "%g -> %g Hz", start, settled);
    }
    // 808 snare's two networks at 175 and 331 Hz (as recorded); the rim shot's at 455 and 1667 Hz.
    {
        const auto sd = alone (2, 0, { { "SD Snappy", 0.0 }, { "SD Pitch Env", 0.0 } });
        CHECK (partialAt (sd, 0, 4800, 175.0, 48000.0));
        CHECK (powerAt (sd, 0, 4800, 331.0, 48000.0) < 0.05 * powerAt (sd, 0, 4800, 175.0, 48000.0)); // the second far under, as recorded
        const auto bright = alone (2, 0, { { "SD Snappy", 0.0 }, { "SD Pitch Env", 0.0 }, { "SD Tone", 1.0 } });
        CHECK (partialAt (bright, 0, 4800, 331.0, 48000.0)); // Tone brings it up
        const auto rs = alone (1, 0, { { "RS Noise", 0.0 }, { "RS Click", 0.0 }, { "RS Decay", 0.5 } });
        CHECK (partialAt (rs, 0, 2400, 455.0, 48000.0) && partialAt (rs, 0, 2400, 1667.0, 48000.0));
    }
    // 808 toms: the pitch falls as the ring dies (the diodes).
    {
        const auto lt = alone (4, 0, { { "LT Color", 0.0 }, { "LT Decay", 0.8 } });
        const double early = crossingsHz (lt, 0, 2400, 48000.0), late = crossingsHz (lt, 14400, 28800, 48000.0);
        CHECK_MSG (early > late * 1.03 && std::fabs (late - 91.0) < 3.0, "%g -> %g Hz", early, late);
    }
    // 808 hats: their energy above 6 kHz (the 7.1 kHz band, clipped and high passed).
    {
        const auto ch = alone (7, 0, {});
        CHECK (powerAt (ch, 0, 2400, 9000.0, 48000.0) > 10.0 * powerAt (ch, 0, 2400, 1500.0, 48000.0));
    }
    // The 808 clap's bursts: at the defaults (Claps 4, Speed 50 %: 9 ms apart) each starts with a jump in level, and
    // nothing new comes after the fourth.
    {
        const auto cp = alone (3, 0, { { "CP Mix", -1.0 } });
        const size_t gap = 432; // 9 ms
        for (int b = 1; b < 4; ++b)
        {
            const size_t at = gap * (size_t) b;
            CHECK_MSG (energyOf (cp, at, at + 48) > 20.0 * energyOf (cp, at - 48, at), "burst %d", b + 1);
        }
        CHECK (energyOf (cp, 4 * gap, 4 * gap + 48) < energyOf (cp, 4 * gap - 48, 4 * gap) * 1.5);
    }
    // The 909 style's metal: words held for a 30 kHz clock (1.6 samples at 48 kHz) and few of them: 6 bits.
    {
        Instance i;
        auto values = solo (8);
        values.insert (values.end(), { { "OH Model", 1.0 }, { "Clip", 0.0 }, { "OH Level", 0.0 } });
        i.setup (values);
        std::vector<float> l, r;
        i.play (24000, { { 0, padKey (8), 1.0 } }, l, r);
        // Before the output low pass the words repeat; after it, the 30 kHz images are gone but the spectrum keeps
        // a notch near the clock: little power at 30 kHz's image (18 kHz) against 9 kHz.
        CHECK (powerAt (l, 0, 4800, 18000.0, 48000.0) < powerAt (l, 0, 4800, 9000.0, 48000.0));
    }
}

// Velocity: softer is quieter; Vel Sens 0 ignores it. Accent: at or above Accent Vel louder.
void testVelocityAccent()
{
    auto peakAt = [] (double vel, std::vector<std::pair<std::string, double>> extra) {
        Instance i;
        auto values = solo (2);
        values.insert (values.end(), extra.begin(), extra.end());
        i.setup (values);
        std::vector<float> l, r;
        i.play (24000, { { 0, padKey (2), vel } }, l, r);
        return peakOf (l);
    };
    const double soft = peakAt (0.3, {}), loud = peakAt (0.75, {});
    CHECK (soft < loud * 0.6);
    CHECK (std::fabs (peakAt (0.3, { { "SD Vel Sens", 0.0 } }) - peakAt (0.75, { { "SD Vel Sens", 0.0 } })) < 1e-6);
    const std::vector<std::pair<std::string, double>> flat = { { "SD Vel Sens", 0.0 }, { "SD Level", -12.0 }, { "Clip", 0.0 } };
    auto with = [&] (std::pair<std::string, double> extra) { auto v = flat; v.push_back (extra); return v; };
    const double below = peakAt (99.0 / 127.0, flat), accented = peakAt (100.0 / 127.0, flat);
    CHECK_MSG (accented > below * 1.15, "%g %g", accented, below);
    CHECK (std::fabs (peakAt (100.0 / 127.0, with ({ "Accent", 0.0 })) - below) < 1e-6);
}

// Choke: the closed hat cuts the open one (group 1); a voice out of the group does not; the host's choke event cuts.
void testChoke()
{
    auto render = [] (std::vector<std::pair<std::string, double>> values, std::vector<Note> notes, std::vector<float>& l, bool hostChoke = false) {
        Instance i;
        auto base = solo (8);
        base.push_back ({ "CH Level", -60.0 });
        base.insert (base.end(), values.begin(), values.end());
        i.setup (base);
        std::vector<float> r;
        i.render (48000, 128, l, r, [] (uint64_t) { return Transport {}; }, [&] (uint64_t at, uint32_t n, Events& e) {
            for (const auto& nt : notes)
                if (nt.frame >= at && nt.frame < at + n)
                    e.note ((uint32_t) (nt.frame - at), CLAP_EVENT_NOTE_ON, (int16_t) nt.key, nt.velocity);
            if (hostChoke && 12000 >= at && 12000 < at + n)
                e.note ((uint32_t) (12000 - at), CLAP_EVENT_NOTE_CHOKE, padKey (8), 0.0);
        });
    };
    std::vector<float> open, choked, free, cut;
    render ({ { "OH Decay", 0.8 } }, { { 0, padKey (8) } }, open);
    render ({ { "OH Decay", 0.8 } }, { { 0, padKey (8) }, { 12000, padKey (7) } }, choked);
    render ({ { "OH Decay", 0.8 }, { "CH Choke", 0.0 } }, { { 0, padKey (8) }, { 12000, padKey (7) } }, free);
    render ({ { "OH Decay", 0.8 } }, { { 0, padKey (8) } }, cut, true);
    CHECK (energyOf (open, 14400, 24000) > 1e-4);
    CHECK (peakOf (choked, 14400) < 1e-6); // the CH is silent here (its level); the OH gone within the choke time
    CHECK (same (std::vector<float> (open.begin(), open.end()), free));
    CHECK (peakOf (cut, 14400) < 1e-6);
    CHECK (same (std::vector<float> (open.begin(), open.begin() + 12000), std::vector<float> (cut.begin(), cut.begin() + 12000)));
}

// A voice played in one style then hit in another carries nothing of the first: a 909 closed hat still ringing, the
// Model turned to 606 and the hat hit again, against the 606 hat hit fresh (the oscillators' phases differ, so the
// levels are compared, not the samples).
void testModelSwitch()
{
    auto render = [] (bool first909) {
        Instance i;
        auto values = solo (7);
        values.insert (values.end(), { { "CH Model", first909 ? 1.0 : 2.0 }, { "CH Decay", 1.0 }, { "Clip", 0.0 } });
        i.setup (values);
        const clap_id model = i.id ("CH Model");
        std::vector<float> l, r;
        i.render (12000, 128, l, r, [] (uint64_t) { return Transport {}; }, [&] (uint64_t at, uint32_t n, Events& e) {
            if (first909 && at == 0)
                e.note (0, CLAP_EVENT_NOTE_ON, padKey (7), 0.8);
            if (at <= 2000 && 2000 < at + n)
                e.param ((uint32_t) (2000 - at), model, 2.0);
            if (at <= 2400 && 2400 < at + n)
                e.note ((uint32_t) (2400 - at), CLAP_EVENT_NOTE_ON, padKey (7), 0.8);
            e.sort();
        });
        return l;
    };
    const auto switched = render (true), fresh = render (false);
    const double a = energyOf (switched, 2400, 12000), b = energyOf (fresh, 2400, 12000);
    CHECK_MSG (b > 1e-6 && a < 1.25 * b && a > 0.8 * b, "switched %.4g, fresh %.4g", a, b);
}

// The TR-1000's generator controls Foundry adds in 0.5.0: Phase, the reformer's amount (Bend Mix), Mute Decay, the
// start delay (Nudge to 80 ms), the 909 rim's O1-O3 Tune and Decay, the 909 tom's Detail; and the toms' Attack and
// Tone.
void testGeneratorControls()
{
    auto alone = [] (int voice, int model, std::vector<std::pair<std::string, double>> extra, uint64_t frames = 24000) {
        Instance i;
        auto values = solo (voice);
        values.insert (values.end(), { { std::string (tags[voice]) + " Model", (double) model }, { "Clip", 0.0 } });
        values.insert (values.end(), extra.begin(), extra.end());
        i.setup (values);
        std::vector<float> l, r;
        i.play (frames, { { 0, padKey (voice), 0.75 } }, l, r);
        return l;
    };
    // A four-pole split at `hz`: the energy above it (or below it).
    auto split = [] (const std::vector<float>& x, size_t from, size_t to, double hz, bool high) {
        const double a = 1.0 - std::exp (-2.0 * M_PI * hz / 48000.0);
        double lp[4] {}, e = 0.0;
        for (size_t i = 0; i < std::min (to, x.size()); ++i)
        {
            double y = x[i];
            for (auto& s : lp)
            {
                s += (y - s) * a;
                y = high ? y - s : s;
            }
            if (i >= from)
                e += y * y;
        }
        return e;
    };
    // Phase: Inverse is the same voice upside down, bit for bit.
    {
        const auto normal = alone (0, 0, {}), inverse = alone (0, 0, { { "BD Phase", 1.0 } });
        bool flipped = peakOf (normal) > 0.1;
        for (size_t k = 0; k < normal.size(); ++k)
            flipped = flipped && inverse[k] == -normal[k];
        CHECK (flipped);
    }
    // Bend Mix: at 0 the bend is out (the dry voice), at 100 % all of it, half way between.
    {
        const auto dry = alone (2, 0, {});
        const auto none = alone (2, 0, { { "SD Bend", 1.0 }, { "SD Bend Type", 0.0 }, { "SD Bend Mix", 0.0 } });
        const auto full = alone (2, 0, { { "SD Bend", 1.0 }, { "SD Bend Type", 0.0 } });
        const auto half = alone (2, 0, { { "SD Bend", 1.0 }, { "SD Bend Type", 0.0 }, { "SD Bend Mix", 0.5 } });
        CHECK (same (dry, none));
        CHECK (! same (dry, full) && ! same (full, half) && ! same (dry, half));
    }
    // Mute Decay: the open hat cut by the closed hat at 12000 fades over its own 200 ms instead of the kit's ~11 ms.
    {
        auto cut = [] (double mute) {
            Instance i;
            auto values = solo (8);
            values.insert (values.end(), { { "CH Level", -60.0 }, { "OH Decay", 0.8 }, { "OH Mute Decay", mute } });
            i.setup (values);
            std::vector<float> l, r;
            i.play (36000, { { 0, padKey (8), 0.8 }, { 12000, padKey (7), 0.8 } }, l, r);
            return l;
        };
        const auto kit = cut (0.0), slow = cut (1.0);
        CHECK (peakOf (kit, 14400) < 1e-6);
        CHECK (energyOf (slow, 14400, 19200) > 1e-4 && peakOf (slow, 33600) < 1e-3);
    }
    // Nudge reaches 80 ms: 3840 frames at 48 kHz, the TR-1000's longest start delay.
    {
        Instance i;
        auto values = solo (0);
        values.push_back ({ "BD Nudge", 80.0 });
        i.setup (values);
        std::vector<float> l, r;
        i.play (12000, { { 600, padKey (0), 0.8 } }, l, r);
        CHECK (onsetOf (l) == 600 + 3840);
    }
    // O1-O3 Tune: the 808 rim's 455 Hz network an octave down at 0 %, its 1667 Hz one an octave up at 100 %; the
    // 909 rim's third (990 Hz) moves too.
    {
        const std::vector<std::pair<std::string, double>> bare = { { "RS Noise", 0.0 }, { "RS Click", 0.0 }, { "RS Decay", 0.5 } };
        auto with = [&] (std::vector<std::pair<std::string, double>> more) { more.insert (more.begin(), bare.begin(), bare.end()); return more; };
        const auto low = alone (1, 0, with ({ { "RS O1 Tune", 0.0 } }));
        CHECK (partialAt (low, 0, 2400, 227.5, 48000.0) && ! partialAt (low, 0, 2400, 455.0, 48000.0));
        const auto high = alone (1, 0, with ({ { "RS O2 Tune", 1.0 } }));
        CHECK (partialAt (high, 0, 2400, 3334.0, 48000.0) && partialAt (high, 0, 2400, 455.0, 48000.0));
        const auto three = alone (1, 1, with ({}));
        const auto moved = alone (1, 1, with ({ { "RS O3 Tune", 1.0 } }));
        CHECK (partialAt (moved, 0, 4800, 1980.0, 48000.0) && powerAt (moved, 0, 4800, 990.0, 48000.0) < 0.25 * powerAt (three, 0, 4800, 990.0, 48000.0));
        // O1-O3 Decay: the 909 rim's rings, longer at 100 %, shorter at 0 %.
        const auto ring = alone (1, 1, with ({ { "RS O1 Decay", 1.0 }, { "RS O2 Decay", 1.0 }, { "RS O3 Decay", 1.0 } }));
        const auto dead = alone (1, 1, with ({ { "RS O1 Decay", 0.0 }, { "RS O2 Decay", 0.0 }, { "RS O3 Decay", 0.0 } }));
        CHECK (energyOf (ring, 2400, 9600) > 4.0 * energyOf (three, 2400, 9600));
        CHECK (energyOf (dead, 2400, 9600) < 0.25 * energyOf (three, 2400, 9600));
    }
    // Toms: Detail lifts the highs, Attack the first milliseconds; Tone below 0 darkens, above 0 thins.
    for (int model = 0; model < 3; ++model)
    {
        const auto plain = alone (4, model, {});
        const auto detail = alone (4, model, { { "LT Detail", 1.0 } });
        const auto attack = alone (4, model, { { "LT Attack", 1.0 } });
        const auto dark = alone (4, model, { { "LT Tone", -1.0 } }), thin = alone (4, model, { { "LT Tone", 1.0 } });
        CHECK_MSG (split (detail, 0, 9600, 2000.0, true) > 3.0 * split (plain, 0, 9600, 2000.0, true), "model %d", model);
        CHECK_MSG (energyOf (attack, 0, 96) > 1.5 * energyOf (plain, 0, 96), "model %d", model); // the click, then gone:
        CHECK_MSG (std::fabs (energyOf (attack, 480, 960) / energyOf (plain, 480, 960) - 1.0) < 0.05, "model %d", model);
        CHECK_MSG (split (dark, 0, 9600, 1000.0, true) < 0.2 * split (plain, 0, 9600, 1000.0, true), "model %d", model);
        CHECK_MSG (split (thin, 0, 9600, 60.0, false) < 0.5 * split (plain, 0, 9600, 60.0, false), "model %d", model);
    }
}

// Voices that end when their sound does, whatever was played before; pitches far past the design range; the host's
// edges (its chokes, bar lines on the nearest frame, a slow Tape delay).
void testEnds()
{
    // A voice is over once it has died away: a later hit fades in again with Attack Time (a voice still going would
    // not). `firstMs` of the later hit against the first's.
    auto again = [] (int voice, std::vector<std::pair<std::string, double>> values, std::vector<Note> notes, size_t later) {
        Instance i;
        auto all = solo (voice);
        all.push_back ({ std::string (tags[voice]) + " Attack Time", 0.3 });
        all.insert (all.end(), values.begin(), values.end());
        i.setup (all);
        std::vector<float> l, r;
        i.render (later + 4800, 128, l, r, [] (uint64_t) { return Transport {}; }, [&] (uint64_t at, uint32_t n, Events& e) {
            for (const auto& nt : notes)
                if (nt.frame >= at && nt.frame < at + n)
                {
                    if (nt.key < 0) // a Model change
                        e.param ((uint32_t) (nt.frame - at), i.id (std::string (tags[voice]) + " Model"), nt.velocity);
                    else
                        e.note ((uint32_t) (nt.frame - at), CLAP_EVENT_NOTE_ON, (int16_t) nt.key, nt.velocity);
                }
            e.sort();
        });
        return l;
    };
    // Toms and conga at their default Osc Mix (one network): a second network was struck, never rung, never let go.
    for (int voice : { 4, 5, 6, 14 })
        for (int model : { 0, 2 })
        {
            const int key = padKey (voice);
            const auto l = again (voice, { { std::string (tags[voice]) + " Model", (double) model } }, { { 0, key, 0.8 }, { 144000, key, 0.8 } }, 144000);
            const double first = energyOf (l, 0, 96), later = energyOf (l, 144000, 144096);
            CHECK_MSG (first > 0.0 && std::fabs (later / first - 1.0) < 0.05, "%s model %d: %g then %g", tags[voice], model, first, later);
        }
    // A crash hit as the 808's, then the 606's: the 808's top band envelope must not keep it going. The claves hit as
    // the 909's, then the 808's: nor the 909's second network.
    for (auto [voice, from, to] : { std::tuple { 9, 0.0, 2.0 }, std::tuple { 12, 1.0, 0.0 } })
    {
        const int key = padKey (voice);
        const auto l = again (voice, { { std::string (tags[voice]) + " Model", from } }, { { 0, key, 0.8 }, { 2000, -1, to }, { 2400, key, 0.8 }, { 240000, key, 0.8 } }, 240000);
        const auto fresh = again (voice, { { std::string (tags[voice]) + " Model", to } }, { { 0, key, 0.8 } }, 0);
        CHECK_MSG (energyOf (l, 240000, 240096) < 3.0 * energyOf (fresh, 0, 96), "%s", tags[voice]);
    }
    // The 909 rim's third network rings out even when it is the last: no cut while it is still sounding.
    {
        Instance i;
        auto values = solo (1);
        values.insert (values.end(), { { "RS Model", 1.0 }, { "RS Noise", 0.0 }, { "RS Click", 0.0 }, { "RS O1 Decay", 0.0 }, { "RS O2 Decay", 0.0 }, { "RS O3 Decay", 1.0 }, { "Clip", 0.0 } });
        i.setup (values);
        std::vector<float> l, r;
        i.play (9600, { { 0, padKey (1), 0.8 } }, l, r);
        const double a = energyOf (l, 960, 1920), b = energyOf (l, 1920, 2880);
        CHECK_MSG (a > 1e-4 && b > 0.005 * a && b < a, "%g %g", a, b); // (cut, the second was 0)
    }
    // Far past the design range (the Keys voice at its top): the kick and conga ring as they should, and die.
    {
        auto keys = [] (int voice, std::vector<std::pair<std::string, double>> more, int key, uint64_t frames) {
            Instance i;
            auto values = solo (voice);
            values.insert (values.end(), { { "Keys Voice", (double) (voice + 1) }, { "Clip", 0.0 } });
            values.insert (values.end(), more.begin(), more.end());
            i.setup (values);
            std::vector<float> l, r;
            i.play (frames, { { 0, key, 0.8 } }, l, r);
            return l;
        };
        const auto bd = keys (0, { { "BD Attack", 1.0 } }, 127, 24000);
        CHECK_MSG (finiteBounded (bd, 4.0) && peakOf (bd, 480, 2400) > 1e-3, "BD %g", peakOf (bd, 480, 2400));
        const auto cg = keys (14, { { "CG Osc Mix", 0.0 } }, 127, 4 * 48000);
        CHECK_MSG (finiteBounded (cg, 4.0) && energyOf (cg, 3 * 48000) < 1e-8, "CG %g", energyOf (cg, 3 * 48000));
    }
    // The hats' oscillators past a cycle a sample: their phases stay whole, the next ordinary hit is as loud as ever.
    for (int model = 0; model < 3; ++model)
    {
        auto run = [&] (bool high) {
            Instance i;
            auto values = solo (7);
            values.insert (values.end(), { { "CH Model", (double) model }, { "CH Tune", 12.0 }, { "Keys Voice", 8.0 }, { "Clip", 0.0 } });
            i.setup (values);
            std::vector<float> l, r;
            std::vector<Note> notes = { { 96000, padKey (7), 0.8 } };
            if (high)
                notes.insert (notes.begin(), { 0, 127, 0.8 });
            i.play (120000, notes, l, r);
            return peakOf (l, 96000);
        };
        const double after = run (true), fresh = run (false);
        CHECK_MSG (after < 2.0 * fresh, "model %d: %g against %g", model, after, fresh);
    }
    // The 909 kick's coupling and the bend's DC blocker keep their corners at any rate: B2 as loud at 192 kHz.
    {
        auto rms = [] (double rate) {
            Instance i (rate);
            auto values = solo (15);
            values.insert (values.end(), { { "Clip", 0.0 }, { "B2 Bend", 0.4 } });
            i.setup (values);
            std::vector<float> l, r;
            i.play ((uint64_t) (0.3 * rate), { { 0, padKey (15), 0.8 } }, l, r);
            return std::sqrt (energyOf (l) / (double) l.size());
        };
        const double db = 20.0 * std::log10 (rms (192000.0) / rms (48000.0));
        CHECK_MSG (std::fabs (db) < 0.5, "%.2f dB", db);
    }
    // The host's choke reaches the voices linked to the pad, and key -1 chokes every one.
    for (int key : { padKey (0), -1 })
    {
        Instance i;
        auto values = solo (0);
        values.insert (values.end(), { { "SD Level", 0.0 }, { "SD Link", 1.0 }, { "SD Decay", 1.0 }, { "BD Decay", 1.0 } });
        i.setup (values);
        std::vector<float> l, r;
        i.render (36000, 128, l, r, [] (uint64_t) { return Transport {}; }, [&] (uint64_t at, uint32_t n, Events& e) {
            if (at == 0)
                e.note (0, CLAP_EVENT_NOTE_ON, (int16_t) padKey (0), 0.8);
            if (at <= 12000 && 12000 < at + n)
                e.note ((uint32_t) (12000 - at), CLAP_EVENT_NOTE_CHOKE, (int16_t) key, 0.0);
        });
        CHECK_MSG (energyOf (l, 12000, 14400) > 1e-6 && peakOf (l, 16800) < 1e-6, "key %d: %g", key, peakOf (l, 16800));
    }
    // Cycle 1/2 at 140 BPM with each downbeat on the frame nearest it (a hair before the bar line, half the time):
    // bars 0, 2, 4 and 6 play.
    {
        Instance i;
        auto values = solo (0);
        values.push_back ({ "BD Cycle", 1.0 });
        i.setup (values);
        const double bar = 4.0 * 60.0 / 140.0 * 48000.0;
        std::vector<Note> notes;
        for (int b = 0; b < 8; ++b)
            notes.push_back ({ (uint64_t) std::llround (b * bar), padKey (0), 0.8 });
        std::vector<float> l, r;
        i.play ((uint64_t) (8.5 * bar), notes, l, r, true, 128, 140.0);
        int pattern = 0;
        for (int b = 0; b < 8; ++b)
            pattern |= energyOf (l, (size_t) std::llround (b * bar), (size_t) std::llround (b * bar) + 480) > 1e-4 ? 1 << b : 0;
        CHECK_MSG (pattern == 0x55, "bars played %02x", pattern);
    }
    // Tape at its longest (1/2 at 40 BPM): the wow stays inside the buffer (make sanitize would stop on a stray read).
    {
        Instance i;
        auto values = solo (0);
        values.insert (values.end(), { { "Delay Mode", 2.0 }, { "Delay Time", 11.0 }, { "Delay Feedback", 0.6 }, { "BD Delay", 1.0 } });
        i.setup (values);
        std::vector<Note> notes;
        for (int k = 0; k < 8; ++k)
            notes.push_back ({ (uint64_t) k * 72000, padKey (0), 0.9 });
        std::vector<float> l, r;
        i.play (12 * 48000, notes, l, r, true, 128, 40.0);
        CHECK (finiteBounded (l, 1.0) && energyOf (l, 5 * 48000) > 1e-3);
    }
}

// Prob, Cycle, Flam, Sub-Step, Nudge, Link and Humanize.
void testPerformance()
{
    auto hits = [] (std::vector<std::pair<std::string, double>> values, int count, bool playing, int key = padKey (1), uint64_t every = 12000) {
        Instance i;
        auto base = solo (1);
        base.insert (base.end(), values.begin(), values.end());
        i.setup (base);
        std::vector<Note> notes;
        for (int k = 0; k < count; ++k)
            notes.push_back ({ (uint64_t) k * every + 600, key, 0.75 });
        std::vector<float> l, r;
        i.play ((uint64_t) count * every + 12000, notes, l, r, playing);
        return l;
    };
    // Prob 0: nothing; 100: all; 50: some, the same twice.
    CHECK (peakOf (hits ({ { "RS Prob", 0.0 } }, 16, true)) == 0.0);
    const auto all = hits ({}, 16, true);
    CHECK (onsetsOf (all, 1e-4, 2000).size() == 16);
    const auto half1 = hits ({ { "RS Prob", 0.5 } }, 64, true), half2 = hits ({ { "RS Prob", 0.5 } }, 64, true);
    const size_t played = onsetsOf (half1, 1e-4, 2000).size();
    CHECK_MSG (played > 16 && played < 48, "played %zu of 64", played);
    CHECK (same (half1, half2));
    // Stopped, chance still applies (counted by hits).
    const size_t stopped = onsetsOf (hits ({ { "RS Prob", 0.5 } }, 64, false), 1e-4, 2000).size();
    CHECK (stopped > 16 && stopped < 48);

    // Cycle 2/2 at 120 bpm (a bar is 96000 frames): hits in the second bar of each pair only.
    {
        const auto c = hits ({ { "RS Cycle", 2.0 } }, 16, true, padKey (1), 24000); // four a bar, four bars
        const auto on = onsetsOf (c, 1e-4, 2000);
        CHECK (on.size() == 8);
        bool rightBars = true;
        for (long f : on)
            rightBars &= ((f / 96000) % 2) == 1;
        CHECK (rightBars);
        // Stopped, cycles do not apply: pads play.
        CHECK (onsetsOf (hits ({ { "RS Cycle", 2.0 } }, 8, false, padKey (1), 24000), 1e-4, 2000).size() == 8);
        // 1st Only: just the first bar after Play.
        const auto first = onsetsOf (hits ({ { "RS Cycle", 18.0 } }, 16, true, padKey (1), 24000), 1e-4, 2000);
        CHECK (first.size() == 4 && first.back() < 96000);
    }
    // Flam 20 ms: a grace hit on the note (0.45 of its velocity), the main one 960 frames later, louder (the claves:
    // linear, so the levels compare).
    {
        Instance fl;
        auto values = solo (12);
        values.push_back ({ "CL Flam", 20.0 });
        fl.setup (values);
        std::vector<float> f, r;
        fl.play (24000, { { 600, padKey (12), 0.75 } }, f, r, true);
        Instance g;
        g.setup (solo (12));
        std::vector<float> grace;
        g.play (f.size(), { { 600, padKey (12), (double) (0.75f * 0.45f) } }, grace, r, true);
        CHECK (onsetOf (f) == 600);
        std::vector<float> main (f.size());
        for (size_t k = 0; k < f.size(); ++k)
            main[k] = f[k] - grace[k];
        CHECK_MSG (onsetOf (main, 0, 1e-7) == 600 + 960, "%ld", onsetOf (main, 0, 1e-7));
        CHECK (peakOf (main) > peakOf (grace) * 1.2);
    }
    // Sub-Step 4 at Step 1/16, 120 bpm: four hits 1500 frames apart.
    {
        const auto s = hits ({ { "RS Sub-Step", 3.0 }, { "RS Decay", 0.0 } }, 1, true);
        const auto on = onsetsOf (s, 1e-4, 400);
        CHECK (on.size() == 4);
        if (on.size() == 4)
            CHECK (on[0] == 600 && on[1] == 2100 && on[2] == 3600 && on[3] == 5100);
    }
    // Nudge 10 ms: 480 frames late.
    CHECK (onsetOf (hits ({ { "RS Nudge", 10.0 } }, 1, true)) == 600 + 480);
    // Humanize: late (never early) by up to 8 ms, the same twice.
    {
        const auto a = hits ({ { "Humanize", 1.0 } }, 16, true), b = hits ({ { "Humanize", 1.0 } }, 16, true);
        CHECK (same (a, b));
        const auto on = onsetsOf (a, 1e-4, 2000);
        bool late = on.size() == 16;
        for (size_t k = 0; k < on.size(); ++k)
            late &= on[k] >= (long) (k * 12000 + 600) && on[k] <= (long) (k * 12000 + 600 + 384);
        CHECK (late);
    }
    // Link: B2 linked to BD sounds with it, the sum of the two alone.
    {
        auto one = [] (std::vector<std::pair<std::string, double>> values, std::vector<Note> notes) {
            Instance i;
            auto base = solo (-1);
            base.push_back ({ "BD Level", 0.0 });
            base.push_back ({ "B2 Level", -3.0 });
            base.push_back ({ "Clip", 0.0 });
            base.insert (base.end(), values.begin(), values.end());
            i.setup (base);
            std::vector<float> l, r;
            i.play (24000, notes, l, r);
            return l;
        };
        const auto both = one ({ { "B2 Link", 1.0 } }, { { 100, padKey (0) } });
        const auto separate = one ({}, { { 100, padKey (0) }, { 100, padKey (15) } });
        double diff = 0.0;
        for (size_t k = 0; k < both.size(); ++k)
            diff = std::max (diff, (double) std::fabs (both[k] - separate[k]));
        CHECK_MSG (diff < 1e-6, "linked differs by %g", diff);
        CHECK (energyOf (both) > energyOf (one ({}, { { 100, padKey (0) } })) * 1.2);
    }
}

// Keys voice: notes outside the pads play the chosen voice transposed from note 60; GM key map.
void testKeysAndMaps()
{
    auto pitch = [] (int key, std::vector<std::pair<std::string, double>> extra) {
        Instance i;
        auto values = solo (0);
        values.insert (values.end(), { { "Keys Voice", 1.0 }, { "BD Decay", 1.0 }, { "BD Body Depth", 0.0 }, { "BD Attack", 0.0 } });
        values.insert (values.end(), extra.begin(), extra.end());
        i.setup (values);
        std::vector<float> l, r;
        i.play (48000, { { 0, key, 0.75 } }, l, r);
        return crossingsHz (l, 4800, 48000, 48000.0);
    };
    const double c3 = pitch (60, {}), c4 = pitch (72, {}), pad = pitch (36, {});
    CHECK_MSG (std::fabs (c3 - 53.0) < 1.0, "%g", c3);
    CHECK_MSG (std::fabs (c4 / c3 - 2.0) < 0.02, "%g %g", c3, c4);
    CHECK (std::fabs (pad - c3) < 0.5);
    CHECK_MSG (std::fabs (pitch (36, { { "BD Tune", 12.0 } }) / c3 - 2.0) < 0.02, "tune");
    CHECK_MSG (std::fabs (pitch (36, { { "Kit Tune", -12.0 } }) / c3 - 0.5) < 0.02, "kit tune");
    {
        Instance i;
        i.setup (solo (0));
        std::vector<float> l, r;
        i.play (24000, { { 0, 60, 0.75 } }, l, r);
        CHECK (peakOf (l) == 0.0); // no Keys voice: nothing
    }
    // GM: 42 is the closed hat, 46 the open one, 35 the second kick; in Pads 42 is the crash (pad A07), 39 the closed
    // hat (A04).
    for (auto [key, voice, gm] : std::vector<std::tuple<int, int, bool>> { { 42, 7, true }, { 46, 8, true }, { 35, 15, true }, { 42, 9, false }, { 39, 7, false } })
    {
        Instance i;
        auto values = solo (voice);
        values.push_back ({ "Key Map", gm ? 1.0 : 0.0 });
        i.setup (values);
        std::vector<float> l, r;
        i.play (12000, { { 0, key, 0.75 } }, l, r);
        CHECK_MSG (peakOf (l) > 0.05, "key %d voice %d", key, voice);
    }
    // MIDI notes as CLAP notes.
    {
        Instance a, b;
        a.setup (solo (2)), b.setup (solo (2));
        std::vector<float> la, ra, lb, rb;
        a.render (12000, 128, la, ra, [] (uint64_t) { return Transport {}; }, [] (uint64_t at, uint32_t, Events& e) { if (at == 0) e.note (10, CLAP_EVENT_NOTE_ON, padKey (2), 100.0 / 127.0); });
        b.render (12000, 128, lb, rb, [] (uint64_t) { return Transport {}; }, [] (uint64_t at, uint32_t, Events& e) { if (at == 0) e.midi (10, 0x90, (uint8_t) padKey (2), 100); });
        CHECK (same (la, lb) && peakOf (la) > 0.05);
    }
}

// The side chain: BD as source ducks the open hat by its Duck.
void testSideChain()
{
    auto hat = [] (double duck) {
        Instance i;
        auto values = solo (8);
        values.insert (values.end(), { { "BD Level", -60.0 }, { "SC Source", 1.0 }, { "OH Duck", duck }, { "OH Decay", 1.0 }, { "OH Model", 1.0 } });
        i.setup (values);
        std::vector<float> l, r;
        i.play (48000, { { 0, padKey (8), 0.75 }, { 12000, padKey (0), 0.75 } }, l, r);
        return l;
    };
    const auto plain = hat (0.0), ducked = hat (0.8);
    CHECK (same (std::vector<float> (plain.begin(), plain.begin() + 12000), std::vector<float> (ducked.begin(), ducked.begin() + 12000)));
    CHECK (energyOf (ducked, 12100, 14000) < energyOf (plain, 12100, 14000) * 0.2);
    CHECK (energyOf (ducked, 40000, 48000) > energyOf (plain, 40000, 48000) * 0.8); // released

    // The SC Key port: the kick alone (SC Source's default), before its Level, whatever else plays; silent with SC
    // Source Off.
    {
        Instance a, b;
        a.setup ({ { "BD Level", -60.0 } });
        b.setup ({ { "SC Source", 0.0 } });
        std::vector<float> l, r;
        const std::vector<Note> notes = { { 0, padKey (0), 0.9 }, { 12000, padKey (2), 0.9 }, { 12000, padKey (7), 0.9 } };
        a.play (24000, notes, l, r);
        CHECK (peakOf (a.keyL, 0, 6000) > 0.1 && same (a.keyL, a.keyR));
        CHECK (peakOf (l, 0, 6000) < 1e-3); // muted in the mix, still keying
        Instance solo0;
        auto alone = solo (0);
        alone.push_back ({ "Clip", 0.0 });
        solo0.setup (alone);
        std::vector<float> l0, r0;
        solo0.play (24000, { { 0, 36, 0.9 } }, l0, r0);
        CHECK (same (a.keyL, solo0.keyL)); // the snare and hat are not in it
        double diff = 0.0;
        for (size_t k = 0; k < l0.size(); ++k)
            diff = std::max (diff, (double) std::fabs (l0[k] - solo0.keyL[k]));
        CHECK_MSG (diff < 1e-6, "key differs from the solo kick at 0 dB by %g", diff);
        b.play (24000, notes, l, r);
        CHECK (peakOf (b.keyL) == 0.0);
    }
    // Comp Key: SC Source: the kick keys the bus compressor; the hat dips under it though the kick is muted.
    auto keyed = [] (double key) {
        Instance i;
        auto values = solo (8);
        values.insert (values.end(), { { "BD Level", -60.0 }, { "OH Decay", 1.0 }, { "Comp", 0.8 }, { "Comp Key", key }, { "Comp Attack", 0.0 } });
        i.setup (values);
        std::vector<float> l, r;
        i.play (48000, { { 0, padKey (8), 0.75 }, { 12000, padKey (0), 1.0 } }, l, r);
        return l;
    };
    const auto mixKeyed = keyed (0.0), kickKeyed = keyed (1.0);
    CHECK (energyOf (kickKeyed, 12200, 16000) < energyOf (mixKeyed, 12200, 16000) * 0.5);
    // Keyed, no makeup: before the kick the hat is as it is with no compressor at all.
    {
        Instance d;
        auto values = solo (8);
        values.insert (values.end(), { { "BD Level", -60.0 }, { "OH Decay", 1.0 }, { "Comp", 0.0 } });
        d.setup (values);
        std::vector<float> dl, dr;
        d.play (48000, { { 0, padKey (8), 0.75 }, { 12000, padKey (0), 1.0 } }, dl, dr);
        double diff = 0.0;
        for (size_t k = 0; k < 11000; ++k)
            diff = std::max (diff, (double) std::fabs (dl[k] - kickKeyed[k]));
        CHECK_MSG (diff < 1e-6, "keyed comp moved the hat before the kick by %g", diff);
    }
}

// The effects: reverb and delay tails, the analog filter darkening, the compressor, Clip.
void testEffects()
{
    auto clap = [] (std::vector<std::pair<std::string, double>> values) {
        Instance i;
        auto base = solo (3);
        base.insert (base.end(), values.begin(), values.end());
        i.setup (base);
        std::vector<float> l, r;
        i.play (96000, { { 0, padKey (3), 0.75 } }, l, r, true);
        return std::make_pair (l, r);
    };
    const auto dry = clap ({});
    const auto wet = clap ({ { "CP Reverb", 0.6 }, { "Reverb Time", 0.6 } });
    CHECK (energyOf (wet.first, 48000, 96000) > 1e3 * energyOf (dry.first, 48000, 96000) + 1e-6);
    CHECK (! same (wet.first, wet.second)); // a stereo tail
    const auto echo = clap ({ { "CP Delay", 0.6 }, { "Delay Time", 8.0 }, { "Delay Feedback", 0.5 } }); // 1/4: 24000 frames
    CHECK (peakOf (echo.first, 23000, 30000) > 0.1 * peakOf (dry.first));
    // Ping-pong: the first echo left only, the second right.
    CHECK_MSG (peakOf (echo.second, 23000, 30000) < 1e-3, "%g", peakOf (echo.second, 23000, 30000));
    CHECK_MSG (peakOf (echo.second, 47000, 54000) > 0.005, "%g", peakOf (echo.second, 47000, 54000));
    const auto dark = clap ({ { "Analog Filter", -0.8 } });
    CHECK (energyOf (dark.first) < energyOf (dry.first) * 0.5);
    const auto squashed = clap ({ { "Comp", 1.0 }, { "CP Level", 6.0 } });
    CHECK (finiteBounded (squashed.first, 1.0));
    const auto limited = clap ({ { "CP Level", 6.0 }, { "Clip", 2.0 }, { "Volume", 6.0 } });
    CHECK (peakOf (limited.first) <= 0.98 + 1e-6);
}

// The same output whatever the block size, with notes, knob moves, an LFO, flams, sub-steps and the effects.
void testBlockSizes()
{
    auto run = [] (uint32_t block) {
        Instance i;
        i.setup ({ { "BD LFO Target", 1.0 }, { "BD LFO Depth", 0.4 }, { "SD Flam", 15.0 }, { "CH Sub-Step", 2.0 }, { "CP Reverb", 0.5 },
                   { "OH Delay", 0.4 }, { "Comp", 0.5 }, { "SC Source", 1.0 }, { "CH Duck", 0.6 }, { "Analog Filter", -0.4 },
                   { "LT Flt Type", 1.0 }, { "LT Flt Env", 0.6 }, { "MT Bend", 0.6 }, { "MT Bend Type", 0.8 }, { "Humanize", 0.5 } });
        std::vector<float> l, r;
        i.render (48000 * 3, block, l, r, [&] (uint64_t at) { return Transport { true, 120.0, (double) at * 2.0 / 48000.0 }; },
                  [&] (uint64_t at, uint32_t n, Events& e) {
                      for (uint64_t f = 0; f < 48000 * 3; f += 3000)
                          if (f >= at && f < at + n)
                          {
                              const int step = (int) (f / 3000);
                              const uint32_t t = (uint32_t) (f - at);
                              e.note (t, CLAP_EVENT_NOTE_ON, (int16_t) (36 + (step * 7) % 16), 0.5 + 0.1 * (step % 5));
                              if (step % 4 == 0) e.note (t, CLAP_EVENT_NOTE_ON, 36, 1.0);
                              if (step % 3 == 0) e.param (t, i.id ("SD Tune"), (double) (step % 7) - 3.0);
                          }
                  });
        return l;
    };
    const auto a = run (128);
    for (uint32_t b : { 1u, 37u, 4096u })
        CHECK_MSG (same (a, run (b)), "block %u", b);

    // Settings and notes on the very first frame, playing from the top (as the fingerprint sends them).
    auto first = [] (uint32_t block, std::vector<float>& key) {
        Instance i;
        std::vector<float> l, r;
        const std::vector<std::pair<std::string, double>> values = { { "CP Reverb", 0.4 }, { "OH Delay", 0.3 }, { "SD Flam", 12.0 },
                                                                     { "CH Sub-Step", 1.0 }, { "BD LFO Target", 1.0 }, { "BD LFO Depth", 0.3 } };
        i.render (48000 * 3, block, l, r, [&] (uint64_t at) { return Transport { true, 120.0, (double) at * 2.0 / 48000.0 }; },
                  [&] (uint64_t at, uint32_t n, Events& e) {
                      if (at == 0)
                          for (auto& [name, v] : values)
                              e.param (0, i.id (name), v);
                      for (uint64_t f = 0; f < 48000 * 3; f += 6000)
                          if (f >= at && f < at + n)
                          {
                              e.note ((uint32_t) (f - at), CLAP_EVENT_NOTE_ON, (int16_t) (36 + (f / 6000) % 16), 0.8);
                              e.note ((uint32_t) (f - at), CLAP_EVENT_NOTE_ON, padKey (7), 0.6);
                          }
                  });
        key = i.keyL;
        return l;
    };
    std::vector<float> k128, kb;
    const auto f128 = first (128, k128);
    for (uint32_t b : { 1u, 37u, 4096u })
    {
        const auto fb = first (b, kb);
        long at = -1;
        for (size_t k = 0; k < f128.size() && at < 0; ++k)
            if (f128[k] != fb[k])
                at = (long) k;
        CHECK_MSG (at < 0 && same (k128, kb), "block %u: first difference at frame %ld", b, at);
    }
}

// Random and extreme settings: finite and bounded (Clip on), at three rates.
void testExtremes()
{
    std::mt19937 rng (11);
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int trial = 0; trial < 12; ++trial)
        {
            Instance i (rate);
            std::vector<std::pair<std::string, double>> values;
            for (uint32_t k = 0; k < i.params->count (i.plugin); ++k)
            {
                clap_param_info info {};
                i.params->get_info (i.plugin, k, &info);
                double u = std::uniform_real_distribution<double> (0, 1) (rng);
                if (trial % 3 == 1) u = u < 0.5 ? 0.0 : 1.0; // the ends
                double v = info.min_value + (info.max_value - info.min_value) * u;
                if (info.flags & CLAP_PARAM_IS_STEPPED) v = std::round (v);
                if (std::strcmp (info.name, "Clip") == 0) v = 1.0 + (trial % 2);
                if (std::strcmp (info.name, "Volume") == 0) v = std::min (v, 0.0);
                values.push_back ({ info.name, v });
            }
            i.setup (values);
            std::vector<Note> notes;
            for (int k = 0; k < 64; ++k)
                notes.push_back ({ (uint64_t) (k * rate / 16.0), 36 + (int) (rng() % 64), 0.2 + 0.8 * std::uniform_real_distribution<double> (0, 1) (rng) });
            std::vector<float> l, r;
            i.play ((uint64_t) (rate * 5.0), notes, l, r, true);
            CHECK_MSG (finiteBounded (l, 1.0) && finiteBounded (r, 1.0), "rate %g trial %d", rate, trial);
        }
}

// Silence costs nothing and stays silence: no notes, zeros.
void testSilence()
{
    Instance i;
    std::vector<float> l, r;
    i.play (48000, {}, l, r, true);
    CHECK (peakOf (l) == 0.0 && peakOf (r) == 0.0);
}

void writeDemos (const std::string& folder)
{
    for (const char* key : { "default", "808-kit", "909-kit", "606-kit", "606-mods", "bent-808", "techno-909", "808-bass", "big-room" })
    {
        Instance i;
        i.loadPreset (key);
        std::vector<Note> notes;
        const uint64_t step = 6000; // 1/16 at 120 bpm
        for (int bar = 0; bar < 2; ++bar)
            for (int s = 0; s < 16; ++s)
            {
                const uint64_t f = (uint64_t) (bar * 16 + s) * step;
                if (s % 4 == 0 || s == 14) notes.push_back ({ f, padKey (0), s == 0 ? 1.0 : 0.85 });
                if (s == 4 || s == 12) notes.push_back ({ f, padKey (2), 0.9 });
                if (s == 12 && bar == 1) notes.push_back ({ f, padKey (3), 0.9 });
                notes.push_back ({ f, padKey (s % 4 == 2 ? 8 : 7), s % 2 ? 0.55 : 0.8 });
                if (s == 10) notes.push_back ({ f, padKey (11), 0.7 });
                if (bar == 1 && s >= 12) notes.push_back ({ f, padKey (4 + (s - 12) % 3), 0.8 });
                if (std::strcmp (key, "808-bass") == 0 && s % 8 == 0) notes.push_back ({ f, s == 0 ? 60 : 63, 0.9 });
            }
        notes.push_back ({ step * 32, padKey (9), 0.9 });
        notes.push_back ({ step * 32, padKey (0), 1.0 });
        std::vector<float> l, r;
        i.play (step * 32 + 96000, notes, l, r, true);
        writeWav ((folder + "/demo-" + key + ".wav").c_str(), l, r, 48000);
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: host Foundry.clap OUT_FOLDER\n");
        return 2;
    }
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::printf ("FAIL dlopen: %s\n", dlerror());
        return 1;
    }
    entry = static_cast<const clap_plugin_entry*> (dlsym (lib, "clap_entry"));
    if (entry == nullptr || ! entry->init (argv[1]))
    {
        std::printf ("FAIL clap_entry\n");
        return 1;
    }
    factory = static_cast<const clap_plugin_factory*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));

    testDescription();
    testDrumMap (argv[1]);
    testParameters();
    testState();
    testPresets();
    testVoices();
    testCharacter();
    testVelocityAccent();
    testChoke();
    testModelSwitch();
    testGeneratorControls();
    testEnds();
    testPerformance();
    testKeysAndMaps();
    testSideChain();
    testEffects();
    testBlockSizes();
    testExtremes();
    testSilence();
    writeDemos (argv[2]);

    entry->deinit();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
