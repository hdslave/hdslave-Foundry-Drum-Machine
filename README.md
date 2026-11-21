# Foundry

A sixteen-voice drum machine in the style of the 808, 909 and 606, as a CLAP plug-in, built for the Akai MPC Sample
running Melodee (64-bit ARM Linux, Cortex-A55). Hosts list it as **hdslave Foundry** (id `com.hdslave.foundry`).

Every voice is a circuit type (kick, snare, tom, rim, clap, hat, cymbal, cowbell, claves, maracas) with three models
(808, 909 and 606 style) and the controls the TR-1000 gives that instrument across its analog, circuit-bent (8X/9X)
and 606 models, plus a shared chain the TR-1000 has per instrument (bend, drive, filter with its envelope, amp
attack and gate, sends), plus its step features (probability, cycles, flam, sub-steps, nudge) applied to the notes
the host plays. The 606 kit gets the controls its classic hardware mods add (see [The 606 and its mods](#the-606-and-its-mods)).

It has no sequencer of its own: the host plays it. In Melodee, a plug-in track on Foundry plays it from its pads as a
kit (Foundry carries the pad map Melodee looks for), and Melodee's step sequencer, swing, note repeat and MOTION do
what the TR's own would.

## The kit

Pads A01-A16 (MIDI notes 36-51), each a voice; **Key Map: GM** moves them to General MIDI keys instead.

| Pad | Note | Voice | Type | Opens as | GM keys |
|---|---|---|---|---|---|
| A01 | 36 | BD Bass Drum | kick | 808 | 36 |
| A02 | 37 | RS Rim Shot | rim | 808 | 37 |
| A03 | 38 | SD Snare Drum | snare | 909 | 38, 40 |
| A04 | 39 | CP Hand Clap | clap | 808 | 39 |
| A05 | 40 | LT Low Tom | tom | 808 | 41, 43 |
| A06 | 41 | MT Mid Tom | tom | 808 | 45, 47 |
| A07 | 42 | HT High Tom | tom | 808 | 48, 50 |
| A08 | 43 | CH Closed Hat | hat | 808 | 42, 44 |
| A09 | 44 | OH Open Hat | hat | 808 | 46 |
| A10 | 45 | CY Crash | cymbal | 808 | 49, 52, 55, 57 |
| A11 | 46 | RD Ride | cymbal | 909 | 51, 53, 59 |
| A12 | 47 | CB Cowbell | cowbell | 808 | 56 |
| A13 | 48 | CL Claves | clave | 808 | 75, 76, 77 |
| A14 | 49 | MA Maracas | maraca | 808 | 70, 69 |
| A15 | 50 | CG Conga | tom | 808 | 62, 63, 64 |
| A16 | 51 | B2 Bass Drum 2 | kick | 909 | 35 |

**Keys Voice** (Off, or any voice) makes every other note play that voice transposed: with Pads, note 60 is the voice
at its own pitch (pad bank B, notes 52-67, is -8 to +7 semitones; bank C goes on up); with GM, notes from 84 up, 96
being its pitch. Set it to BD for an 808 bass line (the **808 Bass Keys** preset).

## The models

They are in the style of the machines, not copies of their circuits:

- **808 style**: resonators struck by the hit, the way a bridged-T rings: a second hit adds to a ring still going. The
  kick's pitch sweeps from Body Depth over Body Decay with a click through a low pass; the metal is the six square
  oscillators into two bands and a high pass.
- **909 style**: oscillators reset by the hit, swept, through decay envelopes, with a click and a noise burst on the
  kick; the hats and cymbals carry a few-bit ROM grit (Error starts at 45 %).
- **606 style**: lighter, higher and shorter, with more noise in the metal.

Voices a machine never had (a 909 or 606 cowbell, a 606 clap or rim) are new ones in that model's character.

## Controls

Melodee lists them in rows of three: the kit's first, then each voice's block (each starts a row with its Level),
then the effects.

### The kit

| Control | Range | |
|---|---|---|
| Volume | -inf to +6 dB | after everything |
| Kit Tune | ±12 st | every voice |
| Kit Decay | ±100 % | every decay, ×0.35 to ×2.8 |
| Accent | 0 – 100 % | how much an accented hit lifts (the TR's accent level), times each voice's Accent |
| Accent Vel | 1 – 127 | a note at least this hard is accented |
| Choke Time | 1 – 200 ms | how fast a choked or gated voice fades (the TR-1000's MUTE DECAY) |
| Clip | Off / Soft / Limit | the TR-1000's master clip |
| Key Map | Pads / GM | |
| Keys Voice | Off / a voice | see above |
| Step | 1/8 … 1/32 with triplets | the step Sub-Step divides |
| Vel Curve | Soft / Linear / Hard | |
| Humanize | 0 – 100 % | each hit up to 8 ms late and up to 25 % softer, the same for the same place in the song |

### Each voice

Every voice has Level, Tune (±24 st), Decay, its type's own controls (below), Model, Pan, and the shared chain:

| Control | Range | |
|---|---|---|
| Bend Type, Bend | 0 – 100 % | the TR-1000's Reformer: four characters in a row (fold, rectify, crush, decimate), Bend Type the place between them, Bend the depth |
| Drive | 0 – 100 % | |
| Flt Type, Cutoff, Reso | Off / LP / HP / BP, 20 Hz – 20 kHz | |
| Flt Env, Flt Decay | ±6 octaves, 5 ms – 2 s | the filter's envelope, from each hit |
| Attack Time | 0 – 100 ms | a fade in (from silence only, so a retrigger does not click) |
| Gate | Off, 5 ms – 2 s | the TR-1000's hold: the voice fades (Choke Time) this long after the hit |
| Vel Sens, Accent | 0 – 100 % | |
| Reverb, Delay | 0 – 100 % | sends |
| Duck | 0 – 100 % | how far the side chain (SC Source) pushes it down |
| Choke | Off / 1 – 4 | a voice cuts the others in its group; CH and OH are both in group 1 |
| Link | Off / a voice | also plays when that voice's pad plays (stack B2 under BD: the TR-1000's layers) |
| Nudge | 0 – 50 ms | later |
| Prob | 0 – 100 % | how likely each hit plays; the same hits drop when the same part of the song plays again |
| Flam | Off, 1 – 60 ms | a grace hit at 45 % of the velocity, then the hit this much later |
| Sub-Step | 1, 2, 3, 4, 6, 8 | the hit split into this many across one Step at the host's tempo |
| Cycle | Every, n/2, n/3, n/4, n/8, 1st Only | plays in the nth of every m bars counted from the song's top (the TR-1000's step cycle, by bars); 1st Only: the first bar after Play. Stopped, it always plays |
| LFO Target, Wave, Rate, Depth | Tune, Decay, Cutoff, Level, Pan, Bend, Drive; sine, triangle, saw, square, random; 8 bars – 1/32 | in step with the song while it plays |

### Each type's own

The TR-1000 names where it has the control; *new* where it does not.

| Type | Controls |
|---|---|
| Kick | Attack (click: the 808's TONE, the 909's ATTACK), Tone (the click's brightness, *new*), Body Depth, Body Decay (the pitch sweep: the 909's TUNE, 8X BODY, 9X P.AMOUNT/P.DECAY), Excite |
| Snare | Tone, Snappy, Snappy Decay, Detune, Osc Mix, Pitch Env, Pitch Decay, Freq Mod |
| Tom, Conga | Color, Color Tone, Color Decay, Pitch Env, Pitch Decay (*new* on toms), Detune, Osc Mix, Shape |
| Rim | Tone, Body, Noise, Freq Mod, Click (*new*) |
| Clap | Claps, Speed, Clap Decay, Mix, Filter, Nuance, Size (the 808's CLAP SIZE: thicker or thinner), Tail Tone (*new*) |
| Hat | Tone, Sculpt, XMod, Blend, Detune, Amp Mod, Error, Noise (*new*) |
| Cymbal | Tone, Sculpt, Filter, Blend, Detune, Amp Mod, Error, Noise (*new*) |
| Cowbell | Detune, Osc Mix, XMod, Ratio, Noise, Shape, Unison (±12 st, 0 off), Tone (*new*) |
| Claves, Maracas | Attack, Tone (*new*) |

Decay (each voice's) runs over that model's range: for example the 808 kick 0.12 – 4 s to fall 60 dB, the closed hat
20 – 400 ms, the open hat 0.12 – 3 s, the crash 0.3 – 6 s.

### The effects

| Control | |
|---|---|
| Reverb Level, Time (0.3 – 12 s), Pre-Delay (0 – 200 ms), Tone, Low Cut (20 Hz – 1 kHz), Mod | eight lines in a feedback network, gently modulated |
| Delay Level, Time (1/32 – 1/2, triplets and dotted, at the host's tempo), Feedback, Tone, Mode (Mono / Ping-Pong / Tape), Delay > Reverb | |
| SC Source, SC Release | the side chain: see below |
| Comp, Comp Attack, Comp Release, Comp Mix, Comp Key | the bus compressor: Comp sets threshold and ratio together, with makeup |
| Analog Filter, Analog Reso, Analog Drive | the TR-1000's analog FX: one knob from a low pass (left) through off to a high pass (right), then drive |

## The kick into a compressor's side chain

**SC Source** (BD unless set otherwise) is the voice that keys the side chain, three ways:

1. **Duck**: each voice's Duck pushes it down by that much each time the source hits, back over SC Release.
2. **Comp Key: SC Source**: the bus compressor's detector hears the source voice alone instead of the mix, so the
   kick pumps the whole kit through the compressor (no makeup gain in this mode: it only ducks).
3. **The SC Key output**: a second stereo output port carrying the source voice alone, for a host's compressor
   side-chain input.

The key is the voice before its Level, so a kick turned down to -inf in the mix still keys (a "ghost kick").

In Melodee: Melodee's master COMPRESSOR can be keyed by a whole track, or by one pad of a sample kit. A plug-in track
keys it as a whole (every Foundry voice), and Melodee does not yet route a plug-in's second output anywhere. So on
the MPC today, use Foundry's own Duck or Comp Key for kick pumping, or put a second Foundry on its own track playing
only the kick and key Melodee's compressor from that track.

## The 606 and its mods

The stock 606 has a level knob per voice and a global accent; the popular hardware mods add tune, decay, snappy and
tone controls and a stronger accent. Foundry's 606 models have all of those (Tune, Decay, Snappy, Tone, Accent, and
the hats' decays apart), and the **606 Mods** preset dials them in: tuned and longer drums, more snap, a harder
accent, a little drive and crush.

## Presets

Nine factory presets, inside the plug-in: Melodee lists them on the plug-in's PRESETS page. A preset sets every
control.

| Preset | What it is |
|---|---|
| Default | The kit as it opens: 808 drums, a 909 snare and ride, a 909 second kick |
| 808 Kit | Every voice 808, the kick longer, the toms single-toned, B2 a long boom |
| 909 Kit | Every voice 909: the kick with attack and sweep, a snappy snare, swept toms, gritty hats |
| 606 Kit | Every voice 606, as it comes |
| 606 Mods | The 606 with the classic mods dialled in |
| Bent 808 | 808 with the Reformer-style bends, cross-modulated metal and excited kick |
| Techno 909 | 909 with the kick keying the ducking, the bus compressor, delay and reverb sends, analog drive |
| 808 Bass Keys | 808 with the kick as the Keys voice: a bass line on bank B and up |
| Big Room | 909 with a gated, reverbed snare, swept toms in the reverb, a longer reverb |

## Where it differs from the TR-1000, and what is left out

- Hits come from the host: there is no step sequencer, pattern memory, song mode, MOTION or snapshots here (Melodee
  has its own). The TR-1000's step features are applied to notes instead: Prob, Flam, Sub-Step and Nudge per voice;
  Cycle counts bars; Nudge only delays (going early would need latency).
- Per-voice compressors and the eighteen instrument effect types are not there (Bend, Drive, the filter and the bus
  compressor cover the commonest); nor the sampler, PCM, FM and virtual analog engines; nor individual outputs (only
  the SC Key port).
- Layers: the TR-1000's layer tracks (two generators on BD, SD, LT, HT) are done with Link: any voice can sound with
  another's pad.
- The models are new circuits in each machine's style; the ranges are chosen to cover what the TR-1000 documents
  (its parameters are 0 – 100 % or ±100 %), not measured from one.

## Cost

Instructions a 128-frame block takes, from `tools/bench.sh` (callgrind):

| Case | Instructions | Share of one A55 core at 1 instruction a cycle |
|---|---|---|
| All sixteen voices on every 1/16 with long decays, every effect, the compressor, analog filter and drive | about 627,000 | 13 % |
| A beat (kick, snare, hats, clap) with reverb, delay and the compressor | about 134,000 | 2.8 % |
| Stopped | about 3,800 | 0.08 % |

A voice costs nothing once it has died away, and the reverb and delay sleep while they have nothing to play. The
knobs that move between hits are worked out every 16 samples. The real share on the MPC depends on how many
instructions the A55 retires a cycle on this code; Melodee's plug-in load figure shows it.

## Build and test

Needs Docker with a Linux arm64 context (Melodee's `colima-melodee` and its `melodee-builder` image by default).

```sh
./build.sh          # build/Foundry.clap, then the tests (tests/host.cpp), the sound's fingerprint and demo renders build/demo-*.wav
./build.sh bench    # build/bench
tools/bench.sh      # instruction counts (Melodee's melodee-profiler image)
```

The tests load the plug-in through the CLAP API and check: description, ports (the mix and SC Key), note names and the
drum map Melodee reads; every parameter's range, text both ways and default, and each voice's block starting a row;
state save and load (and a state from another version); the presets as preset discovery lists them, each playing;
every voice in every model sounding, starting on its frame, bounded and ending; velocity, accent, choke groups and the
host's choke; Prob (the same twice), Cycle, 1st Only, Flam, Sub-Step, Nudge, Humanize, Link; the Keys voice's pitch,
Tune, Kit Tune, the GM map, MIDI notes; the side chain three ways; the reverb, ping-pong delay, analog filter,
compressor and Limit; the same output whatever the block size (1, 37, 128, 4096 frames); finite, bounded output at
36 random and extreme settings at 44.1, 48 and 96 kHz.

`tests/fingerprint.cpp` then renders 93 fixed scenarios (each preset, each voice in each model, the shared chain,
the performance controls, the GM map, the LFOs, the effects and side chain, knobs moving, other block sizes and
rates, stop and a jump, random patches) and hashes every output sample's bits against `tests/fingerprint.txt`: work
meant to keep the sound, such as a speed-up, must leave every hash as it is. A change meant to change the sound
updates them with `make fingerprint-update` (inside the build image). The hashes hold for Melodee's build image and
these compiler flags.

## Install

Copy `build/Foundry.clap` to `Melodee/Plugins/hdslave Foundry/hdslave Foundry.clap` on the card, then pick it on an
empty track: Stage, Plugin, hdslave Foundry. Its pads become the kit. It needs only libc and libm (glibc 2.35 or
older).
