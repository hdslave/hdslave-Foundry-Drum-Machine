// One of Foundry's sixteen voices: its type's circuit in three models (808, 909 and 606 style), then the chain every
// voice shares (bend, drive, filter with its envelope, attack, gate, choke).
//
// The models are in the style of the machines, not copies of their circuits: the 808-style voices ring resonators
// struck by the hit (a second hit adds to a ring still going, as a bridged-T does), the 909-style ones run
// oscillators reset by the hit through envelopes, with 6-bit grit on the metal, and the 606-style ones are lighter,
// higher and shorter. Voices a machine never had (a 909 cowbell, a 606 clap) are new ones in that model's character.
#pragma once

#include "dsp.h"
#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace foundry
{
constexpr int tick = 16;          // the control period, samples, counted from the engine's first sample
constexpr float silent = 1.6e-5f; // -96 dB: a voice whose sources fall below this is done

// What the engine gives a voice at a hit or a tick.
struct Shared
{
    float rate = 48000.0f, invRate = 1.0f / 48000.0f;
    float kitTune = 0.0f;  // semitones
    float kitDecay = 1.0f; // multiplies every decay
    float chokeCoef = 0.99f;
    double lfoBeats = 0.0; // where the LFOs are: the song's place playing, the engine's own clock stopped
    float duck = 0.0f;     // the side chain's envelope, 0..1
};

// Each type's pitch in each model, as a multiple of the voice's 808 pitch (VoiceSpec::baseHz).
inline constexpr float modelPitch[][modelCount] = {
    { 1.0f, 1.12f, 1.28f },  // kick: 49, 55, 63 Hz
    { 1.0f, 0.875f, 2.3f },  // rim
    { 1.0f, 0.95f, 1.3f },   // snare
    { 1.0f, 1.15f, 1.5f },   // clap (its band's centre)
    { 1.0f, 1.08f, 1.15f },  // tom
    { 1.0f, 1.0f, 1.0f },    // hat (its own oscillator sets)
    { 1.0f, 1.0f, 1.0f },    // cymbal
    { 1.0f, 1.087f, 1.28f }, // cowbell
    { 1.0f, 0.72f, 1.32f },  // clave
    { 1.0f, 1.4f, 1.6f },    // maraca (its high pass)
};

// The metal: six square oscillators. The 808 style's are the classic six; the 909 and 606 sets are denser and
// higher (new sets).
inline constexpr float metalHz[modelCount][6] = {
    { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f },
    { 263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f },
    { 306.0f, 383.0f, 456.0f, 519.0f, 546.0f, 774.0f },
};
// Their filters (Hz): hats two bands and a high pass, cymbals a low band and a high pass.
inline constexpr float hatBand1[] = { 3440.0f, 6500.0f, 7000.0f }, hatBand2[] = { 7100.0f, 10000.0f, 11000.0f },
                       hatHigh[] = { 6000.0f, 7000.0f, 7500.0f };
inline constexpr float cymLow[] = { 3400.0f, 5200.0f, 6000.0f }, cymHigh[] = { 7800.0f, 9000.0f, 9500.0f };
inline constexpr float metalNoise[] = { 0.0f, 0.2f, 0.4f };  // noise each model adds to the metal
inline constexpr float metalError[] = { 0.0f, 0.45f, 0.0f }; // grit each model starts with (the 909 style's ROM)
// Output levels by model, so the kit sits balanced at its default levels.
inline constexpr float hatGain[] = { 7.5f, 9.75f, 6.0f }, cymGain[] = { 1.96f, 4.48f, 2.52f }, clapGain[] = { 4.0f, 2.6f, 3.8f };

struct Voice
{
    int index = 0;
    const VoiceSpec* spec = &voiceSpecs[0];
    Type type = Type::kick;
    float p[V::count] {};
    bool active = false;

    // The hit in hand.
    uint8_t model = 0;
    float amp = 0.0f;      // the hit's strength: velocity, accent
    float hz = 0.0f;       // its main pitch, the LFO aside
    float lfoPitch = 1.0f; // the LFO's pitch multiplier
    float lfo = 0.0f;      // the LFO, times its depth, at the last tick

    // The type's state.
    Resonator res[3];
    float ph[6] {}, dt[6] {}, gain[6] {};
    float e[4] {}, ec[4] {};
    float penv = 0.0f, pcoef = 0.0f, bendMul = 0.0f, ratio = 1.0f;
    float x[8] {}; // the type's constants for the hit
    Svf f[3];
    float lp[2] {}, lpa[2] {};
    Noise noise;
    float prevOsc = 0.0f;
    int32_t count = 0, countNext = 0, burstsLeft = 0, burstGap = 1, rampLen = 1;
    float holdValue = 0.0f, quant = 1.0f;
    int32_t holdLeft = 0, holdLen = 1;

    // The shared chain.
    Svf flt;
    float fenv = 0.0f, fenvTick = 0.0f;
    float bendHeld = 0.0f, bendCount = 0.0f, bendHold = 1.0f, bendQuant = 4096.0f, bendDepth = 0.0f, bendPos = 0.0f;
    bool bendOn = false;
    DcBlock dc;
    float driveGain = 1.0f, driveNorm = 1.0f;
    uint8_t filterMode = 0;
    float attackPos = 1.0f, attackInc = 0.0f;
    int64_t gateLeft = -1;
    float fade = 1.0f;
    bool fading = false;
    // The mix: gains at the tick's start and end (ramped across it): left, right, reverb, delay, and the side-chain
    // key (the voice before its Level and the ducking, so a muted voice still keys).
    float g0[5] {}, g1[5] {};

    float slot (int k) const { return p[V::slot0 + (uint32_t) k]; }

    void init (int i)
    {
        index = i;
        spec = &voiceSpecs[i];
        type = spec->type;
        noise.s = 0x9e3779b9u + 0x85ebca6bu * (uint32_t) (i + 1);
    }

    void clearState()
    {
        for (auto& r : res) r.clear();
        for (auto& v : e) v = 0.0f;
        for (auto& v : f) v.clear();
        lp[0] = lp[1] = 0.0f;
        penv = 0.0f;
        flt.clear();
        dc.clear();
        bendHeld = bendCount = 0.0f;
        holdValue = 0.0f, holdLeft = 0;
        prevOsc = 0.0f;
        burstsLeft = 0;
        count = 0;
    }

    float decaySeconds (float lfoMul) const
    {
        const float* range = spec->decay[model];
        return range[0] * std::pow (range[1] / range[0], p[V::decay]) * lfoMul;
    }

    // --- The LFO and the knobs that move between hits: at every tick while sounding, and at each hit --------------
    float lfoValue (const Shared& sh) const
    {
        const float depth = p[V::lfoDepth];
        if (depth == 0.0f || p[V::lfoTarget] == 0.0f)
            return 0.0f;
        const double rateBeats = lfoRateBeats[(int) p[V::lfoRate]];
        const double phase = sh.lfoBeats / rateBeats;
        const double cycle = std::floor (phase);
        const float t = (float) (phase - cycle);
        float w = 0.0f;
        switch ((int) p[V::lfoWave])
        {
            case 0: w = sin2pi (t); break;
            case 1: w = 1.0f - 4.0f * std::fabs (t - 0.5f); break;
            case 2: w = 1.0f - 2.0f * t; break;
            case 3: w = t < 0.5f ? 1.0f : -1.0f; break;
            default: w = 2.0f * hash01 ((uint64_t) (int64_t) cycle * 131u + (uint64_t) index * 7919u + 1u) - 1.0f; break;
        }
        return w * depth;
    }
    int lfoTarget() const { return (int) p[V::lfoTarget]; }

    // The tick: the LFO, the filter, the bend and drive amounts, the mix gains.
    void control (const Shared& sh, bool fresh)
    {
        lfo = lfoValue (sh);
        const int target = lfoTarget();
        const float pitchMul = target == lfoTune ? exp2Fast (lfo) : 1.0f;
        if (pitchMul != lfoPitch)
        {
            lfoPitch = pitchMul;
            retune();
        }
        // Bend: its type (a place between four characters) and depth.
        bendDepth = clampf (p[V::bend] + (target == lfoBend ? 0.5f * lfo : 0.0f), 0.0f, 1.0f);
        bendOn = bendDepth > 0.0f;
        if (bendOn)
        {
            bendPos = clampf (p[V::bendType], 0.0f, 1.0f) * 3.0f;
            bendQuant = exp2Fast (12.0f - 10.0f * bendDepth);
            bendHold = 1.0f + 31.0f * bendDepth;
        }
        const float drive = clampf (p[V::drive] + (target == lfoDrive ? 0.5f * lfo : 0.0f), 0.0f, 1.0f);
        driveGain = 1.0f + 7.0f * drive;
        driveNorm = drive > 0.0f ? 1.0f / tanhFast (driveGain) : 1.0f;
        filterMode = (uint8_t) p[V::filter];
        if (filterMode != 0)
        {
            const float octaves = 6.0f * p[V::fltEnv] * fenv + (target == lfoCutoff ? 3.0f * lfo : 0.0f);
            const float k = 2.0f - 1.95f * p[V::reso];
            flt.setHz (cutoffHz (p[V::cutoff]) * exp2Fast (clampf (octaves, -12.0f, 12.0f)), k, sh.rate);
            fenv *= fenvTick;
        }
        // The mix.
        float level = dbGain (p[V::level]);
        if (target == lfoLevel)
            level *= std::fmax (0.0f, 1.0f + 0.9f * lfo);
        const float pan = clampf (p[V::pan] + (target == lfoPan ? lfo : 0.0f), -1.0f, 1.0f);
        const float duck = 1.0f - p[V::duck] * sh.duck;
        const float g[5] = { level * std::fmin (1.0f, 1.0f - pan) * duck, level * std::fmin (1.0f, 1.0f + pan) * duck,
                             level * duck * p[V::reverb], level * duck * p[V::delay], 1.0f };
        for (int i = 0; i < 5; ++i)
        {
            g0[i] = fresh ? g[i] : g1[i];
            g1[i] = g[i];
        }
    }

    // --- A hit ------------------------------------------------------------------------------------------------------
    // `velocity` 0..1 after the curve; `accent` how far the accent lifts it (0 none); `transpose` semitones.
    void trigger (float velocity, float accent, float transpose, const Shared& sh)
    {
        const bool wasActive = active;
        if (fading)
            clearState(); // a choked voice's tail is gone, not back at full
        fading = false;
        fade = 1.0f;
        model = (uint8_t) p[V::model];
        invRate = sh.invRate, rate = sh.rate;
        const float vs = p[V::velSens];
        amp = (1.0f - vs + vs * velocity) * (1.0f + accent * p[V::accent]);
        lfo = lfoValue (sh);
        lfoPitch = lfoTarget() == lfoTune ? exp2Fast (lfo) : 1.0f;
        const float decayMul = lfoTarget() == lfoDecay ? exp2Fast (1.5f * lfo) : 1.0f;
        const float semis = p[V::tune] + sh.kitTune + transpose;
        hz = spec->baseHz * modelPitch[(int) type][model] * exp2Fast (semis * (1.0f / 12.0f));
        const float decay = decaySeconds (decayMul) * sh.kitDecay;
        switch (type)
        {
            case Type::kick: hitKick (decay, sh); break;
            case Type::rim: hitRim (decay, sh); break;
            case Type::snare: hitSnare (decay, sh); break;
            case Type::clap: hitClap (decay, sh); break;
            case Type::tom: hitTom (decay, sh); break;
            case Type::hat: hitMetal (decay, sh, false); break;
            case Type::cymbal: hitMetal (decay, sh, true); break;
            case Type::cowbell: hitCowbell (decay, sh); break;
            case Type::clave: hitClave (decay, sh); break;
            case Type::maraca: hitMaraca (decay, sh); break;
        }
        // The shared chain.
        fenv = 1.0f;
        fenvTick = decayCoef (envMs (p[V::fltDecay]) * 0.001f, sh.rate / (float) tick);
        const float attack = attackMs (p[V::attack]);
        if (attack > 0.05f && ! wasActive)
            attackPos = 0.0f, attackInc = 1000.0f / (attack * sh.rate);
        else if (! wasActive)
            attackPos = 1.0f, attackInc = 0.0f;
        gateLeft = p[V::gate] > 0.0f ? (int64_t) (gateMs (p[V::gate]) * 0.001f * sh.rate) : -1;
        active = true;
        control (sh, ! wasActive);
    }

    void choke()
    {
        if (active)
            fading = true;
    }

    // --- Rendering ------------------------------------------------------------------------------------------------
    // n samples of this voice into `out`, then the shared chain. Turns itself off when done, looked at only where a
    // tick ends (`tickEnd`), so the sample it stops on does not depend on how the host cut its blocks.
    void render (float* out, int n, const Shared& sh, bool tickEnd)
    {
        bool alive = true;
        switch (type)
        {
            case Type::kick: alive = renderKick (out, n); break;
            case Type::rim: alive = renderRim (out, n); break;
            case Type::snare: alive = renderSnare (out, n); break;
            case Type::clap: alive = renderClap (out, n); break;
            case Type::tom: alive = renderTom (out, n); break;
            case Type::hat: alive = renderMetal (out, n, false); break;
            case Type::cymbal: alive = renderMetal (out, n, true); break;
            case Type::cowbell: alive = renderCowbell (out, n); break;
            case Type::clave: alive = renderClave (out, n); break;
            case Type::maraca: alive = renderMaraca (out, n); break;
        }
        chain (out, n, sh);
        if (tickEnd && (! alive || (fading && fade < 1e-4f)))
        {
            active = false;
            fading = false;
            clearState();
        }
    }

    // The LFO moved the pitch: oscillators, resonators and filters that are not swept every sample take the new one.
    void retune()
    {
        const float c = hz * lfoPitch * invRate;
        switch (type)
        {
            case Type::kick:
                if (model != m909)
                    res[0].tune (c * (1.0f + bendMul * penv));
                break;
            case Type::snare:
            case Type::tom:
                if (model != m909)
                {
                    res[0].tune (c * (1.0f + bendMul * penv));
                    res[1].tune (c * (1.0f + bendMul * penv) * ratio);
                }
                break;
            case Type::rim:
                res[0].tune (c);
                res[1].tune (c * ratio);
                res[2].tune (c * 2.4f);
                break;
            case Type::clap:
                f[0].setHz (hz * lfoPitch * clapShift, f[0].k, rate);
                f[1].setHz (hz * lfoPitch * clapShift * 0.55f, f[1].k, rate);
                break;
            case Type::hat:
            case Type::cymbal:
                for (int k = 0; k < 6; ++k)
                    dt[k] = metalBase[k] * c;
                break;
            case Type::cowbell: dt[0] = c, dt[1] = c * ratio, dt[2] = c * x[6], dt[3] = c * x[5]; break;
            case Type::clave:
                res[0].tune (c);
                res[1].tune (c * 1.47f);
                break;
            case Type::maraca:
                f[0].setHz (hz * lfoPitch * exp2Fast (slot (1)), 1.2f, rate);
                f[1].setHz (hz * lfoPitch * exp2Fast (slot (1)) * 1.3f, 0.4f, rate);
                break;
        }
    }

    float invRate = 1.0f / 48000.0f, rate = 48000.0f, clapShift = 1.0f;
    float metalBase[6] {}; // the metal oscillators' frequencies over the voice's pitch

    // --- Kick -----------------------------------------------------------------------------------------------------
    // 808: a resonator struck by the hit, its pitch swept down from Body Depth over Body Decay, a click through a low
    // pass (Tone). 909: a sine oscillator, reset by the hit, swept, through a decay envelope, with a click and a noise
    // burst. 606: the 808's resonator, higher, shorter, swept a little further. Excite pushes it toward a square.
    void hitKick (float decay, const Shared& sh)
    {
        static constexpr float bodyOct[] = { 2.0f, 2.5f, 1.6f }, clickGain[] = { 0.9f, 1.0f, 0.7f }, clickTime[] = { 0.004f, 0.003f, 0.003f };
        bendMul = exp2Fast (bodyOct[model] * slot (2)) - 1.0f;
        pcoef = decayCoef (0.006f * std::pow (30.0f, slot (3)), sh.rate);
        penv = 1.0f;
        e[1] = amp * slot (0) * clickGain[model];
        ec[1] = decayCoef (clickTime[model], sh.rate);
        lpa[0] = onePoleCoef (300.0f * std::pow (40.0f, slot (1)), sh.rate);
        x[0] = slot (4);
        if (model == m909)
        {
            ph[0] = 0.0f;
            e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
            e[2] = amp * slot (0) * 0.5f, ec[2] = decayCoef (0.008f, sh.rate);
        }
        else
        {
            res[0].r = decayCoef (decay, sh.rate);
            res[0].tune (hz * lfoPitch * (1.0f + bendMul) * invRate);
            res[0].strike (amp);
        }
    }
    [[gnu::always_inline]] bool renderKick (float* out, int n)
    {
        const float base = hz * lfoPitch * invRate;
        const bool ring = model != m909;
        for (int i = 0; i < n; ++i)
        {
            float fc = base;
            const bool sweeping = penv > 1e-5f;
            if (sweeping)
            {
                penv *= pcoef;
                fc = base * (1.0f + bendMul * penv);
            }
            float y;
            if (ring)
            {
                if (sweeping)
                    res[0].tune (fc);
                y = res[0].run();
            }
            else
            {
                ph[0] += fc;
                ph[0] -= ph[0] >= 1.0f ? 1.0f : 0.0f;
                y = e[0] * tanhFast (1.5f * sin2pi (ph[0])) * 1.0769f;
                e[0] *= ec[0];
            }
            e[1] *= ec[1];
            float click = e[1];
            if (! ring)
            {
                click += e[2] * noise.next();
                e[2] *= ec[2];
            }
            lp[0] += (click - lp[0]) * lpa[0];
            y += lp[0];
            if (x[0] > 0.0f)
                y += x[0] * (tanhFast (3.0f * y) - y);
            out[i] = y;
        }
        const float body = ring ? res[0].size() : e[0];
        return body > silent || e[1] > silent || std::fabs (lp[0]) > silent;
    }

    // --- Rim shot -------------------------------------------------------------------------------------------------
    // Two resonators (three in the 909 style) struck short, clipped (808), a noise tick and a click, high passed.
    void hitRim (float decay, const Shared& sh)
    {
        const float tone = slot (0), body = slot (1);
        static constexpr float ratios[] = { 3.44f, 1.58f, 2.0f };
        ratio = ratios[model];
        res[0].tune (hz * lfoPitch * invRate, decayCoef (decay * (1.0f + 1.5f * body), sh.rate));
        res[1].tune (hz * lfoPitch * ratio * invRate, decayCoef (decay, sh.rate));
        res[0].strike (amp * (0.4f + body));
        res[1].strike (amp);
        if (model == m909)
        {
            res[2].tune (hz * lfoPitch * 2.4f * invRate, decayCoef (decay * 0.6f, sh.rate));
            res[2].strike (amp * 0.6f);
        }
        x[0] = 0.6f + 0.6f * body;
        x[1] = 0.8f * exp2Fast (0.8f * tone);
        x[2] = slot (3); // Freq Mod
        e[1] = amp * slot (2) * 0.6f, ec[1] = decayCoef (0.006f, sh.rate);
        e[2] = amp * slot (4) * 0.6f, ec[2] = decayCoef (0.0012f, sh.rate);
        f[0].setHz (3000.0f, 1.2f, sh.rate);
        f[1].setHz (250.0f * exp2Fast (1.5f * tone), 1.2f, sh.rate);
    }
    [[gnu::always_inline]] bool renderRim (float* out, int n)
    {
        const float base2 = hz * lfoPitch * ratio * invRate;
        const bool three = model == m909, clip = model == m808;
        for (int i = 0; i < n; ++i)
        {
            const float y0 = res[0].run();
            if (x[2] > 0.0f)
                res[1].tune (base2 * (1.0f + x[2] * y0));
            float y = x[0] * y0 + x[1] * res[1].run();
            if (three)
                y += 0.7f * res[2].run();
            y = clip ? 0.6f * tanhFast (2.5f * y) : y;
            const float nz = f[0].hp (noise.next() * e[1]);
            e[1] *= ec[1];
            const float click = e[2];
            e[2] *= ec[2];
            out[i] = f[1].hp (y + nz + click);
        }
        return res[0].size() > silent || res[1].size() > silent || e[1] > silent || e[2] > silent;
    }

    // --- Snare ----------------------------------------------------------------------------------------------------
    // Two tones (resonators, or 909-style oscillators through a decay) swept by Pitch Env, the second at Detune from
    // the first's ratio and frequency-modulated by it (Freq Mod), plus the snappy noise through its filters.
    void hitSnare (float decay, const Shared& sh)
    {
        const float tone = slot (0), mix = slot (4);
        static constexpr float ratios[] = { 1.78f, 1.6f, 1.5f }, pitchOct[] = { 0.8f, 1.6f, 1.0f }, snap[] = { 0.9f, 1.0f, 1.2f },
                               snapTime[] = { 1.0f, 0.9f, 0.7f };
        ratio = ratios[model] * exp2Fast (0.5f * slot (3));
        bendMul = exp2Fast (pitchOct[model] * slot (5)) - 1.0f;
        pcoef = decayCoef (0.004f * std::pow (40.0f, slot (6)), sh.rate);
        penv = 1.0f;
        x[0] = 0.7f * std::fmin (1.0f, 1.0f - mix);
        x[1] = 0.5f * std::fmin (1.0f, 1.0f + mix) * exp2Fast (0.8f * tone);
        x[2] = slot (7);
        if (model == m909)
        {
            ph[0] = ph[1] = 0.0f;
            e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
            e[3] = amp, ec[3] = decayCoef (decay * 0.6f, sh.rate);
        }
        else
        {
            const float c = hz * lfoPitch * (1.0f + bendMul) * invRate;
            res[0].tune (c, decayCoef (decay, sh.rate));
            res[1].tune (c * ratio, decayCoef (decay * 0.7f, sh.rate));
            res[0].strike (amp);
            res[1].strike (amp);
        }
        e[1] = amp * slot (1) * snap[model];
        ec[1] = decayCoef (0.04f * std::pow (20.0f, slot (2)) * snapTime[model], sh.rate);
        if (model == m808)
        {
            f[0].setHz (1800.0f * exp2Fast (1.2f * tone), 1.2f, sh.rate);
            f[1].setHz (9000.0f * exp2Fast (tone), 1.4f, sh.rate);
        }
        else if (model == m909)
        {
            f[0].setHz (4000.0f * exp2Fast (1.5f * tone), 1.0f, sh.rate);
            f[1].setHz (700.0f, 1.4f, sh.rate);
        }
        else
        {
            f[0].setHz (2600.0f * exp2Fast (tone), 1.0f, sh.rate);
            f[1].setHz (11000.0f, 1.4f, sh.rate);
        }
    }
    [[gnu::always_inline]] bool renderSnare (float* out, int n)
    {
        const float base = hz * lfoPitch * invRate;
        const bool vco = model == m909, hpFirst = model != m909;
        for (int i = 0; i < n; ++i)
        {
            float fc = base;
            const bool sweeping = penv > 1e-5f;
            if (sweeping)
            {
                penv *= pcoef;
                fc = base * (1.0f + bendMul * penv);
            }
            float y1, y2;
            if (! vco)
            {
                if (sweeping)
                    res[0].tune (fc);
                y1 = res[0].run();
                if (sweeping || x[2] > 0.0f)
                    res[1].tune (fc * ratio * (1.0f + 1.5f * x[2] * y1));
                y2 = res[1].run();
            }
            else
            {
                ph[0] += fc;
                ph[0] -= ph[0] >= 1.0f ? 1.0f : 0.0f;
                y1 = e[0] * sin2pi (ph[0]);
                e[0] *= ec[0];
                ph[1] += fc * ratio * (1.0f + 1.5f * x[2] * y1);
                ph[1] -= std::floor (ph[1]);
                y2 = e[3] * sin2pi (ph[1]);
                e[3] *= ec[3];
            }
            float nz = noise.next() * e[1];
            e[1] *= ec[1];
            nz = hpFirst ? f[1].lp (f[0].hp (nz)) : f[1].hp (f[0].lp (nz));
            out[i] = x[0] * y1 + x[1] * y2 + nz;
        }
        const float body = vco ? e[0] + e[3] : res[0].size() + res[1].size();
        return body > silent || e[1] > silent;
    }

    // --- Hand clap ------------------------------------------------------------------------------------------------
    // Noise through a band pass, struck Claps times Speed apart, each burst falling over Clap Decay, and a tail that
    // swells across the bursts then falls over Decay; Mix between them, Filter tilts the whole, Size thickens (a
    // second, lower band) or thins (the band higher), Nuance is the band's Q.
    void hitClap (float decay, const Shared& sh)
    {
        const int claps = 1 + (int) std::lrint (slot (0) * 7.0f);
        static constexpr float gapScale[] = { 1.0f, 0.85f, 0.7f };
        const float speed = 1.0f - slot (1);
        const float gapMs = (4.0f + 20.0f * speed * speed) * gapScale[model];
        burstGap = std::max (1, (int32_t) (gapMs * 0.001f * sh.rate));
        burstsLeft = claps - 1;
        count = 0;
        countNext = burstGap;
        rampLen = std::max (1, burstGap * (claps - 1));
        e[0] = amp, ec[0] = decayCoef (0.002f + 0.025f * slot (2), sh.rate);
        e[1] = 0.0f, ec[1] = decayCoef (decay, sh.rate);
        x[0] = std::fmin (1.0f, 1.0f - slot (3)) * 1.6f;
        x[1] = std::fmin (1.0f, 1.0f + slot (3)) * 1.1f;
        const float q = 0.8f + 7.0f * slot (5), size = slot (6);
        clapShift = size < 0.0f ? exp2Fast (-0.7f * size) : 1.0f;
        const float centre = hz * lfoPitch * clapShift;
        f[0].setHz (centre, 1.0f / q, sh.rate);
        f[1].setHz (centre * 0.55f, 1.0f / (0.7f * q), sh.rate);
        x[2] = std::fmax (0.0f, size) * 1.2f;
        lpa[0] = onePoleCoef (800.0f * std::pow (16.0f, slot (7)), sh.rate);
        lpa[1] = onePoleCoef (1500.0f, sh.rate);
        x[3] = slot (4);
        x[4] = amp;
    }
    [[gnu::always_inline]] bool renderClap (float* out, int n)
    {
        const float rampStep = x[4] / (float) rampLen, gain = clapGain[model];
        for (int i = 0; i < n; ++i)
        {
            ++count;
            if (burstsLeft > 0 && count >= countNext)
            {
                e[0] = x[4];
                --burstsLeft;
                countNext += burstGap;
            }
            if (count <= rampLen)
                e[1] = rampStep * (float) count;
            else
                e[1] *= ec[1];
            const float nz = noise.next();
            float b = f[0].bp (nz);
            if (x[2] > 0.0f)
                b += x[2] * f[1].bp (nz);
            lp[0] += (b - lp[0]) * lpa[0];
            float y = x[0] * b * e[0] + x[1] * lp[0] * e[1];
            e[0] *= ec[0];
            lp[1] += (y - lp[1]) * lpa[1];
            y = lp[1] * (1.0f - x[3]) + (y - lp[1]) * (1.0f + x[3]);
            out[i] = y * gain;
        }
        return burstsLeft > 0 || e[0] > silent || count <= rampLen || e[1] > silent;
    }

    // --- Tom and conga ----------------------------------------------------------------------------------------------
    // One or two tones (Osc Mix, Detune: the second at 1.5 times the first) swept down by Pitch Env, Shape toward a
    // square, and Color: a band of noise with its own tone and decay (the 808 toms' ambience).
    void hitTom (float decay, const Shared& sh)
    {
        static constexpr float pitchOct[] = { 0.6f, 1.4f, 0.9f };
        bendMul = exp2Fast (pitchOct[model] * slot (3)) - 1.0f;
        pcoef = decayCoef (0.01f * std::pow (30.0f, slot (4)), sh.rate);
        penv = 1.0f;
        ratio = 1.5f * exp2Fast (0.5f * slot (5));
        const float mix = slot (6);
        x[0] = std::fmin (1.0f, 1.0f - mix);
        x[1] = 0.8f * std::fmin (1.0f, 1.0f + mix);
        x[2] = slot (7);
        x[3] = 1.0f + 6.0f * slot (7);
        x[4] = 1.0f / tanhFast (x[3]);
        if (model == m909)
        {
            ph[0] = ph[1] = 0.0f;
            e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
        }
        else
        {
            const float c = hz * lfoPitch * (1.0f + bendMul) * invRate;
            res[0].tune (c, decayCoef (decay, sh.rate));
            res[1].tune (c * ratio, decayCoef (decay * 0.8f, sh.rate));
            res[0].strike (amp);
            res[1].strike (amp);
        }
        e[1] = amp * slot (0) * 0.5f, ec[1] = decayCoef (0.02f * std::pow (40.0f, slot (2)), sh.rate);
        lpa[0] = onePoleCoef (200.0f * std::pow (32.0f, slot (1)), sh.rate);
    }
    [[gnu::always_inline]] bool renderTom (float* out, int n)
    {
        const float base = hz * lfoPitch * invRate;
        const bool vco = model == m909, second = x[1] > 0.0f, shape = x[2] > 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float fc = base;
            const bool sweeping = penv > 1e-5f;
            if (sweeping)
            {
                penv *= pcoef;
                fc = base * (1.0f + bendMul * penv);
            }
            float y;
            if (! vco)
            {
                if (sweeping)
                {
                    res[0].tune (fc);
                    if (second)
                        res[1].tune (fc * ratio);
                }
                float y1 = res[0].run();
                if (shape)
                {
                    const float size = res[0].size() + 1e-9f;
                    const float w = y1 / size;
                    y1 += x[2] * (tanhFast (x[3] * w) * x[4] - w) * size;
                }
                y = x[0] * y1 + (second ? x[1] * res[1].run() : 0.0f);
            }
            else
            {
                ph[0] += fc;
                ph[0] -= ph[0] >= 1.0f ? 1.0f : 0.0f;
                ph[1] += fc * ratio;
                ph[1] -= std::floor (ph[1]);
                float w = sin2pi (ph[0]);
                if (shape)
                    w += x[2] * (tanhFast (x[3] * w) * x[4] - w);
                y = e[0] * (x[0] * w + x[1] * sin2pi (ph[1]));
                e[0] *= ec[0];
            }
            const float nz = noise.next() * e[1];
            e[1] *= ec[1];
            lp[0] += (nz - lp[0]) * lpa[0];
            lp[1] += (lp[0] - lp[1]) * lpa[0];
            out[i] = y + 2.0f * lp[1];
        }
        const float body = vco ? e[0] : res[0].size() + res[1].size();
        return body > silent || e[1] > silent || std::fabs (lp[1]) > silent;
    }

    // --- Hats and cymbals: the metal ------------------------------------------------------------------------------
    // Six square oscillators, Blend of them (1 to 6), spread by Detune, cross-modulated (XMod: each one's frequency
    // by the one before) and ring-modulated in pairs (Amp Mod), plus Noise; then hats: two bands (Tone moves them,
    // Sculpt sharpens them and leans to the upper) and a high pass; cymbals: a low band (Filter moves it, Sculpt its
    // Q) and the high pass, each with its own decay, Tone between them. Error: the 909 style's few-bit ROM.
    void hitMetal (float decay, const Shared& sh, bool cymbal)
    {
        const float* set = metalHz[model];
        float geo = 1.0f;
        for (int k = 0; k < 6; ++k)
            geo *= set[k];
        geo = std::pow (geo, 1.0f / 6.0f);
        const float tone = slot (0), blend = slot (3), spread = 0.25f + 1.75f * slot (4);
        float sum = 0.0f;
        for (int k = 0; k < 6; ++k)
        {
            metalBase[k] = geo * std::pow (set[k] / geo, spread);
            gain[k] = clampf (1.0f + 5.0f * blend - (float) k, 0.0f, 1.0f);
            sum += gain[k];
        }
        for (int k = 0; k < 6; ++k)
            dt[k] = metalBase[k] * hz * lfoPitch * invRate;
        x[0] = 0.9f * slot (2);
        x[1] = slot (5);
        x[2] = clampf (slot (7) + metalNoise[model], 0.0f, 1.0f);
        x[3] = 1.0f / std::fmax (sum, 1.0f);
        const float err = clampf (slot (6) + metalError[model], 0.0f, 1.0f);
        x[6] = err;
        quant = exp2Fast (11.0f - 7.0f * err);
        holdLen = 1 + (int32_t) (err * 3.0f);
        e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
        if (! cymbal)
        {
            const float shift = exp2Fast (1.2f * tone), q = 0.8f + 5.0f * slot (1);
            f[0].setHz (hatBand1[model] * shift, 1.0f / q, sh.rate);
            f[1].setHz (hatBand2[model] * shift, 1.0f / q, sh.rate);
            f[2].setHz (hatHigh[model] * shift, 1.4f, sh.rate);
            x[4] = 0.5f, x[5] = 0.5f + 0.3f * slot (1);
        }
        else
        {
            const float q = 2.0f * std::pow (4.0f, slot (1));
            f[0].setHz (cymLow[model] * exp2Fast (2.0f * (slot (2) - 0.5f)), 1.0f / q, sh.rate);
            f[1].setHz (cymHigh[model], 1.2f, sh.rate);
            x[4] = 0.6f * (1.0f - tone), x[5] = 0.6f * (1.0f + tone);
            e[1] = amp, ec[1] = decayCoef (decay * 0.45f, sh.rate);
            e[2] = amp * 0.5f, ec[2] = decayCoef (0.03f, sh.rate);
        }
    }
    [[gnu::always_inline]] bool renderMetal (float* out, int n, bool cymbal)
    {
        const float xmod = x[0], ring = x[1], noiseMix = x[2], norm = x[3], level = cymbal ? cymGain[model] : hatGain[model];
        const bool grit = x[6] > 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float prev = prevOsc, sum = 0.0f, o[6];
            for (int k = 0; k < 6; ++k)
            {
                const float d = dt[k] * (1.0f + xmod * prev);
                const float v = squareAt (ph[k], d);
                ph[k] += d;
                ph[k] -= ph[k] >= 1.0f ? 1.0f : 0.0f;
                ph[k] += ph[k] < 0.0f ? 1.0f : 0.0f;
                prev = v;
                o[k] = v;
                sum += gain[k] * v;
            }
            prevOsc = prev;
            float m = sum * norm;
            if (ring > 0.0f)
                m += ring * ((o[0] * o[1] + o[2] * o[3] + o[4] * o[5]) * (1.0f / 3.0f) - m);
            if (noiseMix > 0.0f)
                m += noiseMix * (noise.next() - m);
            float y;
            if (! cymbal)
            {
                y = f[2].hp (x[4] * f[0].bp (m) + x[5] * f[1].bp (m)) * e[0] * level;
                e[0] *= ec[0];
            }
            else
            {
                y = (x[4] * f[0].bp (m) * e[0] + x[5] * f[1].hp (m) * (e[1] + e[2])) * level;
                e[0] *= ec[0], e[1] *= ec[1], e[2] *= ec[2];
            }
            if (grit)
            {
                if (--holdLeft <= 0)
                {
                    holdLeft = holdLen;
                    holdValue = std::rint (y * quant) / quant;
                }
                y = holdValue;
            }
            out[i] = y;
        }
        return e[0] > silent || (cymbal && (e[1] > silent || e[2] > silent));
    }

    // --- Cowbell --------------------------------------------------------------------------------------------------
    // Two oscillators a detuned fifth-and-a-bit apart (Osc Mix, Detune), Shape from square to sine, an optional third
    // (Unison, semitones away), all frequency-modulated by a sine at Ratio times the first (XMod) with Noise in it,
    // through a band pass (Tone) and a fast-then-slow decay.
    void hitCowbell (float decay, const Shared& sh)
    {
        static constexpr float ratios[] = { 1.4815f, 1.4957f, 1.507f }, band[] = { 2640.0f, 3200.0f, 3600.0f };
        ratio = ratios[model] * exp2Fast (0.3f * slot (0));
        const float mix = slot (1);
        x[0] = std::fmin (1.0f, 1.0f - mix);
        x[1] = std::fmin (1.0f, 1.0f + mix);
        x[2] = 0.5f * slot (2); // XMod
        x[3] = slot (4);        // Noise
        x[4] = slot (5);        // Shape
        x[5] = slot (3);        // Ratio
        x[6] = exp2Fast (slot (6) * (1.0f / 12.0f));
        x[7] = std::fmin (1.0f, std::fabs (slot (6)) * 2.0f);
        const float c = hz * lfoPitch * invRate;
        dt[0] = c, dt[1] = c * ratio, dt[2] = c * x[6], dt[3] = c * x[5];
        f[0].setHz (band[model] * exp2Fast (slot (7)), 0.55f, sh.rate);
        e[0] = amp * 0.65f, ec[0] = decayCoef (0.04f, sh.rate);
        e[1] = amp * 0.35f, ec[1] = decayCoef (decay, sh.rate);
    }
    [[gnu::always_inline]] float bellOsc (int k, float d, float shape)
    {
        const float sq = squareAt (ph[k], d);
        const float w = shape > 0.0f ? sq + shape * (sin2pi (ph[k]) - sq) : sq;
        ph[k] += d;
        ph[k] -= std::floor (ph[k]);
        return w;
    }
    [[gnu::always_inline]] bool renderCowbell (float* out, int n)
    {
        const bool unison = x[7] > 0.0f, modulated = x[2] > 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float fm = 1.0f;
            if (modulated)
            {
                const float mod = sin2pi (ph[3]) + x[3] * noise.next();
                ph[3] += dt[3];
                ph[3] -= ph[3] >= 1.0f ? 1.0f : 0.0f;
                fm = 1.0f + x[2] * mod;
            }
            float y = x[0] * bellOsc (0, dt[0] * fm, x[4]) + x[1] * bellOsc (1, dt[1] * fm, x[4]);
            if (unison)
                y += x[7] * bellOsc (2, dt[2] * fm, x[4]);
            out[i] = f[0].bp (y) * (e[0] + e[1]);
            e[0] *= ec[0], e[1] *= ec[1];
        }
        return e[0] + e[1] > silent;
    }

    // --- Claves ---------------------------------------------------------------------------------------------------
    // A high resonator struck short (two in the 909 style: a wood block), Attack a tick of noise on the strike, Tone
    // tilts it.
    void hitClave (float decay, const Shared& sh)
    {
        res[0].tune (hz * lfoPitch * invRate, decayCoef (decay, sh.rate));
        res[0].strike (amp);
        if (model == m909)
        {
            res[1].tune (hz * lfoPitch * 1.47f * invRate, decayCoef (decay * 0.7f, sh.rate));
            res[1].strike (amp * 0.6f);
        }
        e[1] = amp * slot (0) * 0.8f, ec[1] = decayCoef (0.0015f, sh.rate);
        lpa[0] = onePoleCoef (1500.0f, sh.rate);
        x[0] = slot (1);
    }
    [[gnu::always_inline]] bool renderClave (float* out, int n)
    {
        const bool two = model == m909;
        for (int i = 0; i < n; ++i)
        {
            float y = res[0].run() + (two ? 0.7f * res[1].run() : 0.0f) + e[1] * noise.next();
            e[1] *= ec[1];
            lp[0] += (y - lp[0]) * lpa[0];
            out[i] = 1.3f * (lp[0] * (1.0f - x[0]) + (y - lp[0]) * (1.0f + x[0]));
        }
        return res[0].size() + res[1].size() > silent || e[1] > silent;
    }

    // --- Maracas --------------------------------------------------------------------------------------------------
    // High-passed noise (Tune moves the corner, Tone too) that swells over Attack and falls over Decay; the 909
    // style adds a ringing band.
    void hitMaraca (float decay, const Shared& sh)
    {
        const float corner = hz * lfoPitch * exp2Fast (slot (1));
        f[0].setHz (corner, 1.2f, sh.rate);
        f[1].setHz (corner * 1.3f, 0.4f, sh.rate);
        const float attack = 0.5f + 30.0f * slot (0) * slot (0);
        rampLen = std::max (1, (int32_t) (attack * 0.001f * sh.rate));
        count = 0;
        e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
    }
    [[gnu::always_inline]] bool renderMaraca (float* out, int n)
    {
        const bool band = model == m909;
        const float step = 1.0f / (float) rampLen;
        for (int i = 0; i < n; ++i)
        {
            const float nz = noise.next();
            float y = f[0].hp (nz);
            if (band)
                y = 0.6f * y + 0.5f * f[1].bp (nz);
            float g = e[0];
            if (count < rampLen)
                g *= step * (float) ++count;
            else
                e[0] *= ec[0];
            out[i] = y * g;
        }
        return e[0] > silent;
    }

    // --- The shared chain -----------------------------------------------------------------------------------------
    // Bend (the TR-1000's Reformer, as four characters in a row: fold, rectify, crush, decimate; Bend Type is the
    // place between them, Bend the depth), Drive, the filter, then the amplitude: Attack Time, Gate, a choke's fade.
    [[gnu::always_inline]] float bendChar (int k, float v)
    {
        switch (k)
        {
            case 0:
            {
                float u = (v * (1.0f + 5.0f * bendDepth) + 1.0f) * 0.25f;
                u -= std::floor (u);
                return 1.0f - 4.0f * std::fabs (u - 0.5f);
            }
            case 1: return v + bendDepth * (1.5f * std::fabs (v) - v);
            case 2: return std::rint (v * bendQuant) / bendQuant;
            default: return bendHeld;
        }
    }
    void chain (float* out, int n, const Shared& sh)
    {
        const int seg = std::min (2, (int) bendPos);
        const float frac = bendPos - (float) seg;
        const bool decimating = bendOn && seg + 1 >= 3;
        for (int i = 0; i < n; ++i)
        {
            float v = out[i];
            if (bendOn)
            {
                if (decimating)
                {
                    bendCount += 1.0f;
                    if (bendCount >= bendHold)
                        bendCount -= bendHold, bendHeld = v;
                }
                const float a = bendChar (seg, v);
                const float b = frac > 0.0f ? bendChar (seg + 1, v) : a;
                v = dc.run (a + (b - a) * frac);
            }
            if (driveGain > 1.0f)
                v = tanhFast (v * driveGain) * driveNorm;
            if (filterMode != 0)
            {
                float low, band;
                flt.run (v, low, band);
                v = filterMode == 1 ? low : filterMode == 2 ? v - flt.k * band - low : band * flt.k;
            }
            float g = 1.0f;
            if (attackInc > 0.0f)
            {
                attackPos = std::fmin (1.0f, attackPos + attackInc);
                g = attackPos;
                if (attackPos >= 1.0f)
                    attackInc = 0.0f;
            }
            if (gateLeft >= 0)
            {
                if (gateLeft == 0)
                    fading = true;
                --gateLeft;
            }
            if (fading)
            {
                fade *= sh.chokeCoef;
                g *= fade;
            }
            out[i] = v * g;
        }
    }
};
} // namespace foundry
