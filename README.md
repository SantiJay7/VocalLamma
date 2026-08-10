# Vocal Lamma

**STJ Modules** — Tibetan-monk-style formant vocal synthesizer with a stereo
ping-pong delay, for VCV Rack 2.

![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)

## Overview

Vocal Lamma generates a voice-like glottal source (a blend of a band-limited
sawtooth and a Rosenberg–Klatt glottal pulse), shaped by three parallel resonant
formant filters that morph between five vowels: **OO — OH — AH — AY — EE**.
Subtle breathiness, slow vocal pitch-jitter and a breathing glottal
open-quotient keep the voice organic and alive. A low-feedback ping-pong delay
gives the characteristic spacious "monk chant" sound, and an external audio
input lets you run any signal through the delay.

## Features

- Five-vowel formant synthesizer (OO-OH-AH-AY-EE) with global formant character
- Rosenberg–Klatt glottal pulse blended with a PolyBLEP sawtooth
- Breath noise, slow pitch jitter and open-quotient LFO for realism
- Stereo ping-pong delay (10 ms – 2 s) with feedback and dry/wet mix
- External audio input with level control (EXT IN + INPUT MIX)
- MIDI support: notes, pitch bend (vowel), mod wheel (vibrato), CC 12 (mix), CC 13 (formant)
- Illuminated latching GATE ON button
- 18 HP custom SVG panel with llama logo

See [USER_MANUAL.md](USER_MANUAL.md) for the full manual.

## Build

Requires the [VCV Rack SDK](https://vcvrack.com/Rack) (2.x).

```bash
export RACK_DIR=/path/to/Rack-SDK
make -j$(nproc)
```

Install the plugin by copying `plugin.so`, `plugin.json` and the `res/` folder
into your Rack plugins directory, e.g.
`~/.local/share/Rack2/plugins-lin-x64/VocalLamma/`.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
