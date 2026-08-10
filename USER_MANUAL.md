# Vocal Lamma — User Manual

**Vocal Lamma** is a Tibetan-monk-style formant vocal synthesizer with a stereo
ping-pong delay, for VCV Rack 2.

It generates a voice-like glottal source (a blend of a band-limited sawtooth and
a Rosenberg–Klatt glottal pulse), shaped by three parallel resonant formant
filters that morph between five vowels: **OO — OH — AH — AY — EE**. Subtle
breathiness, slow vocal pitch-jitter and a breathing glottal open-quotient keep
the voice organic and alive. A low-feedback ping-pong delay gives the
characteristic spacious "monk chant" sound. An external audio input lets you run
any signal through the delay.

---

## Quick start

1. Add **Vocal Lamma** to a patch (library → Vocal Lamma → Vocal Lamma).
2. Play a note: send a **V/oct** pitch CV to **PITCH** and a gate to **GATE**
   (press the **GATE ON** button), or simply turn the **PITCH** knob for a drone.
3. Twist **VOWEL** to sweep through the five vowels.
4. Turn up **MIX** on the delay section to hear the echoes.

---

## Front panel

### Voice section (top)

| Control | Type | Description |
| --- | --- | --- |
| **PITCH** | Knob | Coarse pitch, 0–10 V (V/oct). |
| **VOWEL** | Knob | Vowel position: OO → OH → AH → AY → EE. |
| **VOW ATT** | Knob | Amount of CV applied to Vowel (bipolar: −1…+1). |
| **GLIDE** | Knob | Portamento time, 0–5 s. |
| **VIBRATO** | Knob | Vibrato depth (0–1), applied at ~5.5 Hz. |
| **VOICE** | Knob | Voice level (0–1). |
| **FORMANT** | Knob | Global formant character/scale (0.7×–1.3×), shifts brightness/timbre. |
| **GATE ON** | Button | Latching, illuminated push button. Off: voice always sounds. On (lit): voice only sounds while gated. |
| **PITCH** (jack) | Input | V/oct pitch CV. |
| **VIBR CV** | Input | Vibrato CV (−10…+10 V). |
| **VOWEL CV** | Input | Vowel CV (−10…+10 V), scaled by VOW ATT. |
| **GATE** (jack) | Input | Gate/trigger (sounds while > 1 V when GATE ON is active). |

### Delay section (bottom)

| Control | Type | Description |
| --- | --- | --- |
| **TIME** | Knob | Delay time, 10 ms–2 s (logarithmic). |
| **FEEDBACK** | Knob | Feedback/repeats (0–1). Keep below ~0.8 to avoid runaway. |
| **MIX** | Knob | Dry/wet balance of the delay (0–1). |
| **INPUT MIX** | Knob | Level of the external audio input (EXT IN) sent into the delay. |
| **TIME CV** | Input | Delay time CV (−10…+10 V). |
| **EXT IN** | Input | External audio input to the delay. |
| **INPUT CV** | Input | CV for the INPUT MIX level (0–10 V ≈ 0–100 %). |
| **LEFT** | Output | Left audio output. |
| **RIGHT** | Output | Right audio output. |
| **PITCH OUT** | Output | Voice pitch CV (V/oct), glide applied. |
| **LED** | Light | Voice activity; also shows gate state in gate mode. |

---

## MIDI

Connect a MIDI device via the module's right-click context menu
(*MIDI* → *MIDI Input*). The module remembers its MIDI device in the patch.

| MIDI message | Action |
| --- | --- |
| Note on/off | Plays notes (additive): each note restarts the glottal cycle. Pitch follows V/oct. |
| Pitch bend | Sweeps the vowel (bend down → OO, bend up → EE). |
| Mod wheel (CC 1) | Adds vibrato depth. |
| CC 12 | Modulates the delay mix (−64…+63 → 0…1). |
| CC 13 | Modulates the formant character. |

---

## Usage ideas

- **Tibetan monk drone:** set a low VOWEL (OO), turn MIX to ~0.5, FEEDBACK
  ~0.6, TIME ~0.5 s. Feed the PITCH OUT into a second oscillator or LFO for
  chant-like motion.
- **Talking synth:** sequence VOWEL with an LFO or a sequencer and play notes
  via MIDI.
- **Process external audio:** send any sound into EXT IN, set INPUT MIX to 1
  and MIX to taste; the external signal runs through the ping-pong delay while
  the voice remains available.
- **Bass/growl:** keep FORMANT low and glide long for slow, throaty swells.

---

## Patch/cable wiring

- Audio: polyphonic cables carry mults to both LEFT and RIGHT if desired.
- There are no attenuators on CV inputs other than VOW ATT; all CV inputs are
  summed with their knob value (in volts, ±10 V normalized to the 0–1 range).

---

## Technical notes

- Panel: 18 HP, custom SVG with the user's llama logo.
- Source: glottal pulse (Rosenberg–Klatt) blended with a PolyBLEP sawtooth;
  three parallel RBJ band-pass filters with average-male formant frequencies.
- Voice realism: vowel-coloured breath noise (same formant filters), slow pitch
  jitter (~0.08 st, 0.4 s smoothing) and a 0.8 Hz glottal open-quotient LFO.
- Delay: stereo ping-pong, 2^19-sample buffers per channel, interpolated read
  (fractional delay), soft-knee tanh limiting on inputs and outputs.
- Gate smoothing: ~3 ms attack, ~20 ms release; gate mode enabled by the
  latching GATE ON button (illuminated in green).
- Vocal smoothing: ~20 ms vowel glide time.
- The delay time range and buffer size scale with sample rate (up to ~22.8 s
  at 48 kHz in theory; the knob's usable range is 10 ms–2 s).
