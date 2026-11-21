// Foundry's cost: renders patterns through the CLAP entry for SECONDS at 48 kHz in 128-frame blocks, as Melodee
// plays it, and prints the time a block takes here. tools/bench.sh runs it under callgrind for instruction counts
// (exact and repeatable, unlike timings on a computer that is not the MPC).
//   bench [worst|typical|stopped] [SECONDS]
// worst: all sixteen voices hit on every 1/16 with long decays, the effects, the compressor and the analog filter
// on; typical: a beat with the reverb and delay; stopped: the transport stopped and nothing sounding.
#include "../src/foundry.cpp"

#include <chrono>
#include <string>
#include <vector>

int main (int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "worst";
    const double seconds = argc > 2 ? std::atof (argv[2]) : 10.0;
    constexpr double rate = 48000.0;
    constexpr uint32_t block = 128;

    static const clap_host host { CLAP_VERSION, nullptr, "bench", "", "", "1", [] (const clap_host*, const char*) -> const void* { return nullptr; },
                                  [] (const clap_host*) {}, [] (const clap_host*) {}, [] (const clap_host*) {} };
    clap_entry.init ("");
    const auto* factory = static_cast<const clap_plugin_factory*> (clap_entry.get_factory (CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin* plugin = factory->create_plugin (factory, &host, "com.hdslave.foundry");
    plugin->init (plugin);
    plugin->activate (plugin, rate, 1, block);
    plugin->start_processing (plugin);

    using namespace foundry;
    std::vector<std::pair<uint32_t, double>> values;
    if (mode == "worst")
    {
        values = { { M::comp, 0.6 }, { M::afxFilter, -0.3 }, { M::afxDrive, 0.3 }, { M::revTime, 0.7 }, { M::dlyFeedback, 0.6 }, { M::revMod, 0.5 } };
        for (int v = 0; v < voiceCount; ++v)
        {
            values.push_back ({ voiceId (v, V::decay), 1.0 });
            values.push_back ({ voiceId (v, V::reverb), 0.3 });
            values.push_back ({ voiceId (v, V::delay), 0.2 });
            values.push_back ({ voiceId (v, V::filter), 1.0 });
            values.push_back ({ voiceId (v, V::fltEnv), 0.5 });
            values.push_back ({ voiceId (v, V::bend), 0.3 });
            values.push_back ({ voiceId (v, V::bendType), 0.5 });
            values.push_back ({ voiceId (v, V::drive), 0.3 });
            values.push_back ({ voiceId (v, V::lfoTarget), 3.0 });
            values.push_back ({ voiceId (v, V::lfoDepth), 0.5 });
            values.push_back ({ voiceId (v, V::choke), 0.0 });
        }
        values.push_back ({ voiceId (7, V::slot0 + 2), 0.5 }); // CH XMod
        values.push_back ({ voiceId (11, V::slot0 + 2), 0.5 }); // CB XMod
    }
    else if (mode == "typical")
        values = { { voiceId (3, V::reverb), 0.3 }, { voiceId (2, V::reverb), 0.15 }, { voiceId (8, V::delay), 0.2 }, { M::comp, 0.3 } };

    struct Events
    {
        std::vector<clap_event_param_value> params;
        std::vector<clap_event_note> notes;
        std::vector<const clap_event_header*> order;
        clap_input_events in { this, [] (const clap_input_events* e) { return (uint32_t) static_cast<const Events*> (e->ctx)->order.size(); },
                               [] (const clap_input_events* e, uint32_t i) { return static_cast<const Events*> (e->ctx)->order[i]; } };
    };
    clap_output_events out { nullptr, [] (const clap_output_events*, const clap_event_header*) { return true; } };

    std::vector<float> left (block), right (block);
    float* outs[2] = { left.data(), right.data() };
    clap_audio_buffer buffer {};
    buffer.data32 = outs;
    buffer.channel_count = 2;
    const uint64_t blocks = (uint64_t) (seconds * rate / block);
    const uint64_t step = 6000; // 1/16 at 120 bpm
    double sum = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (uint64_t b = 0; b < blocks; ++b)
    {
        Events e;
        if (b == 0)
            for (auto& [id, v] : values)
            {
                clap_event_param_value p {};
                p.header = { sizeof (p), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                p.param_id = id;
                p.note_id = p.port_index = p.channel = p.key = -1;
                p.value = v;
                e.params.push_back (p);
            }
        const uint64_t at = b * block;
        if (mode != "stopped")
            for (uint64_t f = (at + step - 1) / step * step; f < at + block; f += step)
            {
                const int s = (int) (f / step);
                auto add = [&] (int key, double vel) {
                    clap_event_note n {};
                    n.header = { sizeof (n), (uint32_t) (f - at), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0 };
                    n.note_id = -1, n.port_index = 0, n.channel = 0, n.key = (int16_t) key, n.velocity = vel;
                    e.notes.push_back (n);
                };
                if (mode == "worst")
                    for (int k = 0; k < 16; ++k)
                        add (36 + k, 0.8);
                else
                {
                    auto pad = [] (int voice) { return 36 + padOf (voice); }; // the voice's pad
                    if (s % 4 == 0) add (pad (0), 1.0);           // BD
                    if (s % 8 == 4) add (pad (2), 0.9);           // SD
                    add (pad (s % 4 == 2 ? 8 : 7), 0.7);         // OH, CH
                    if (s % 16 == 12) add (pad (3), 0.8);         // CP
                }
            }
        for (auto& p : e.params)
            e.order.push_back (&p.header);
        for (auto& n : e.notes)
            e.order.push_back (&n.header);
        clap_event_transport t {};
        t.header = { sizeof (t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        t.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | (mode != "stopped" ? CLAP_TRANSPORT_IS_PLAYING : 0);
        t.tempo = 120.0;
        t.song_pos_beats = (clap_beattime) std::llround ((double) at * 2.0 / rate * (double) CLAP_BEATTIME_FACTOR);
        clap_process p {};
        p.frames_count = block;
        p.transport = &t;
        p.audio_outputs = &buffer;
        p.audio_outputs_count = 1;
        p.in_events = &e.in;
        p.out_events = &out;
        plugin->process (plugin, &p);
        sum += left[0];
    }
    const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("%s: %llu blocks, %.2f us a block here (checksum %.6f)\n", mode.c_str(), (unsigned long long) blocks, us / blocks, sum);
    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    clap_entry.deinit();
    return 0;
}
