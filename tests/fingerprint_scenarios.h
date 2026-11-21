// The fingerprint's scenarios (included by fingerprint.cpp). New scenarios go at the end; a random scenario draws
// over the parameter list of its version (names below), so parameters added later stay at their defaults there.

// The pad (note) each voice plays on (0.6.0's layout), so the scenarios hit the same voices whatever their pads:
// by voice BD RS SD CP LT MT HT CH OH CY RD CB CL MA CG B2.
constexpr int padOfVoice[16] = { 0, 5, 1, 2, 8, 9, 10, 3, 7, 6, 12, 11, 13, 14, 15, 4 };
constexpr int padKey (int voice) { return 36 + padOfVoice[voice]; }

// A two-bar beat over every voice, with ghost notes and accents, from `start` frames, 1/16 = `step` frames.
inline std::vector<Event> beat (uint64_t step = 6000, int bars = 2, uint64_t start = 0)
{
    std::vector<Event> e;
    for (int b = 0; b < bars; ++b)
        for (int s = 0; s < 16; ++s)
        {
            const uint64_t f = start + (uint64_t) (b * 16 + s) * step;
            if (s % 4 == 0 || s == 14) e.push_back ({ f, Event::noteOn, "", (double) padKey (0), s == 0 ? 1.0 : 0.85 });
            if (s == 4 || s == 12) e.push_back ({ f, Event::noteOn, "", (double) padKey (2), 0.9 });
            if (s == 12) e.push_back ({ f, Event::noteOn, "", (double) padKey (3), 0.8 });
            e.push_back ({ f, Event::noteOn, "", (double) padKey (s % 4 == 2 ? 8 : 7), s % 2 ? 0.5 : 0.8 });
            e.push_back ({ f, Event::noteOn, "", (double) padKey ((b * 16 + s) * 5 % 16), 0.4 + 0.04 * (s % 9) });
        }
    return e;
}

inline std::vector<Scenario> scenarios()
{
    std::vector<Scenario> list;
    // Each preset with the beat.
    for (const char* key : { "default", "808-kit", "909-kit", "606-kit", "606-mods", "bent-808", "techno-909", "808-bass", "big-room" })
    {
        Scenario s;
        s.name = std::string ("preset-") + key;
        s.preset = key;
        s.events = beat();
        if (std::string (key) == "808-bass")
            for (int k = 0; k < 8; ++k)
                s.events.push_back ({ (uint64_t) k * 24000, Event::noteOn, "", (double) (60 + (k * 5) % 12 - 5), 0.9 });
        list.push_back (s);
    }
    // Each voice in each model, alone, three hits (soft, accented, a retrigger inside its ring).
    static const char* const tags[] = { "BD", "RS", "SD", "CP", "LT", "MT", "HT", "CH", "OH", "CY", "RD", "CB", "CL", "MA", "CG", "B2" };
    for (int v = 0; v < 16; ++v)
        for (int m = 0; m < 3; ++m)
        {
            Scenario s;
            s.name = std::string ("voice-") + tags[v] + "-" + std::to_string (m);
            s.seconds = 3.0;
            s.values = { { std::string (tags[v]) + " Model", (double) m } };
            s.events = { { 0, Event::noteOn, "", (double) padKey (v), 0.5 }, { 30000, Event::noteOn, "", (double) padKey (v), 1.0 },
                         { 33000, Event::noteOn, "", (double) padKey (v), 0.7 } };
            list.push_back (s);
        }
    // The shared chain on every voice: bend through its four characters, drive, each filter with its envelope.
    for (int k = 0; k < 4; ++k)
    {
        Scenario s;
        s.name = "chain-" + std::to_string (k);
        for (const char* t : tags)
        {
            const std::string p = t;
            s.values.push_back ({ p + " Bend Type", 0.1 + 0.27 * k });
            s.values.push_back ({ p + " Bend", 0.3 + 0.15 * k });
            s.values.push_back ({ p + " Drive", 0.2 * k });
            s.values.push_back ({ p + " Flt Type", (double) (k % 4) });
            s.values.push_back ({ p + " Flt Env", 0.5 - 0.3 * k });
            s.values.push_back ({ p + " Reso", 0.25 * k });
            s.values.push_back ({ p + " Attack Time", k == 2 ? 0.3 : 0.0 });
            s.values.push_back ({ p + " Gate", k == 3 ? 0.4 : 0.0 });
        }
        s.events = beat();
        list.push_back (s);
    }
    // Performance: prob, cycles, flam, sub-steps, nudge, humanize, links, the Keys voice, the GM map.
    {
        Scenario s;
        s.name = "performance";
        s.seconds = 9.0;
        s.values = { { "CH Prob", 0.6 }, { "SD Flam", 25.0 }, { "OH Sub-Step", 3.0 }, { "RS Cycle", 2.0 }, { "CP Cycle", 15.0 }, { "LT Nudge", 12.0 },
                     { "Humanize", 0.4 }, { "B2 Link", 1.0 }, { "CB Link", 3.0 }, { "Keys Voice", 7.0 }, { "Step", 3.0 } };
        s.events = beat (6000, 4);
        for (int k = 0; k < 12; ++k)
            s.events.push_back ({ (uint64_t) k * 17000 + 500, Event::noteOn, "", (double) (52 + k * 3), 0.8 });
        list.push_back (s);
    }
    {
        Scenario s;
        s.name = "gm-map";
        s.values = { { "Key Map", 1.0 }, { "Keys Voice", 1.0 } };
        for (int k = 0; k < 48; ++k)
            s.events.push_back ({ (uint64_t) k * 5000, Event::noteOn, "", (double) (35 + (k * 7) % 60), 0.3 + 0.014 * k });
        list.push_back (s);
    }
    // LFOs on every target and wave.
    {
        Scenario s;
        s.name = "lfos";
        int k = 0;
        for (const char* t : tags)
        {
            const std::string p = t;
            s.values.push_back ({ p + " LFO Target", (double) (1 + k % 7) });
            s.values.push_back ({ p + " LFO Wave", (double) (k % 5) });
            s.values.push_back ({ p + " LFO Rate", (double) (k % 12) });
            s.values.push_back ({ p + " LFO Depth", k % 2 ? -0.6 : 0.7 });
            s.values.push_back ({ p + " Flt Type", 1.0 });
            s.values.push_back ({ p + " Decay", 0.8 });
            ++k;
        }
        s.events = beat();
        list.push_back (s);
    }
    // The effects and the side chain: reverb, each delay mode, delay into reverb, ducking, the bus compressor keyed
    // by the mix and by the kick, the analog filter both ways, drive, each clip.
    for (int k = 0; k < 6; ++k)
    {
        Scenario s;
        s.name = "effects-" + std::to_string (k);
        s.seconds = 8.0;
        s.values = { { "Delay Mode", (double) (k % 3) }, { "Delay Time", (double) (k * 2 % 12) }, { "Delay Feedback", 0.3 + 0.1 * k },
                     { "Delay > Reverb", k % 2 ? 0.5 : 0.0 }, { "Reverb Time", 0.2 * k }, { "Reverb Mod", 0.2 * k }, { "Reverb Pre-Delay", 30.0 * k },
                     { "Comp", 0.15 * k }, { "Comp Key", (double) (k % 2) }, { "Comp Attack", 0.2 * k }, { "SC Source", (double) (1 + k % 3) },
                     { "SC Release", 0.15 * k }, { "Analog Filter", -0.6 + 0.24 * k }, { "Analog Reso", 0.15 * k }, { "Analog Drive", 0.15 * k },
                     { "Clip", (double) (k % 3) }, { "Volume", -3.0 + k } };
        for (const char* t : tags)
        {
            const std::string p = t;
            s.values.push_back ({ p + " Reverb", 0.1 * (k + 1) });
            s.values.push_back ({ p + " Delay", 0.05 * (k + 1) });
            s.values.push_back ({ p + " Duck", 0.1 * k });
        }
        s.events = beat();
        list.push_back (s);
    }
    // Knobs moving while it plays, a choke from the host, other block sizes and rates, stop and a jump.
    {
        Scenario s;
        s.name = "moving";
        s.events = beat();
        for (int k = 0; k < 40; ++k)
        {
            const uint64_t f = (uint64_t) k * 4567 + 13;
            s.events.push_back ({ f, Event::param, std::string (tags[k % 16]) + " Tune", (double) (k % 9) - 4.0 });
            s.events.push_back ({ f, Event::param, std::string (tags[(k + 5) % 16]) + " Decay", 0.1 + 0.02 * k });
            s.events.push_back ({ f, Event::param, "Kit Decay", -0.5 + 0.025 * k });
        }
        s.events.push_back ({ 50000, Event::choke, "", (double) padKey (8) });
        list.push_back (s);
    }
    for (uint32_t block : { 1u, 37u, 4096u })
    {
        Scenario s;
        s.name = "block-" + std::to_string (block);
        s.block = block;
        s.values = { { "CP Reverb", 0.4 }, { "OH Delay", 0.3 }, { "SD Flam", 12.0 }, { "CH Sub-Step", 1.0 }, { "BD LFO Target", 1.0 }, { "BD LFO Depth", 0.3 } };
        s.events = beat();
        list.push_back (s);
    }
    for (double rate : { 44100.0, 96000.0 })
    {
        Scenario s;
        s.name = "rate-" + std::to_string ((int) rate);
        s.rate = rate;
        s.values = { { "CP Reverb", 0.4 }, { "OH Delay", 0.3 } };
        s.events = beat ((uint64_t) (rate / 8.0));
        list.push_back (s);
    }
    {
        Scenario s;
        s.name = "stop-jump";
        s.seconds = 8.0;
        s.values = { { "RS Cycle", 3.0 }, { "CH Prob", 0.5 }, { "BD LFO Target", 3.0 }, { "BD Flt Type", 1.0 }, { "BD LFO Depth", 0.8 } };
        s.events = beat (6000, 4);
        s.transport = [] (uint64_t at, double bpm, double rate) {
            const double t = (double) at / rate;
            if (t >= 2.0 && t < 3.0) return std::make_pair (false, 4.0);
            if (t >= 3.0) return std::make_pair (true, 16.0 + (t - 3.0) * bpm / 60.0);
            return std::make_pair (true, t * bpm / 60.0);
        };
        list.push_back (s);
    }
    // Random patches (every parameter of 0.1.0) with the beat.
    for (int k = 0; k < 16; ++k)
    {
        Scenario s;
        s.name = "random-" + std::to_string (k);
        std::mt19937 rng (1000 + k);
        auto u = [&] { return std::uniform_real_distribution<double> (0, 1) (rng); };
        for (const char* t : tags)
        {
            const std::string p = t;
            for (const char* n : { " Tune", " Decay", " Bend Type", " Bend", " Drive", " Reso", " Pan" })
                s.values.push_back ({ p + n, std::string (n) == " Tune" ? -12.0 + 24.0 * u() : std::string (n) == " Pan" ? 2.0 * u() - 1.0 : u() });
            s.values.push_back ({ p + " Model", (double) (rng() % 3) });
            s.values.push_back ({ p + " Flt Type", (double) (rng() % 4) });
        }
        s.values.push_back ({ "Comp", u() });
        s.values.push_back ({ "Analog Filter", 2.0 * u() - 1.0 });
        s.values.push_back ({ "Kit Tune", -6.0 + 12.0 * u() });
        s.events = beat();
        list.push_back (s);
    }
    return list;
}
