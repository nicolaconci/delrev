# StereoDelayHall

Audio plugin (VST3 + Audio Unit on macOS): a **stereo delay** with
independent left/right delay times, **time modulation** (rate + depth),
**feedback**, **dry/wet mix**, and a **hall reverb** that is fed **only by the
delay repeats** (never by the dry signal): every repeat gets its own reverb
tail, with dedicated **decay** and **mix** controls. Works on any source —
guitar, vocals, synths, full mixes.

## Parameters

| Parameter  | Range      | Default | Notes                                              |
|------------|------------|---------|----------------------------------------------------|
| Mix L      | 0–100 %    | 30 %    | dry/wet, left channel                              |
| Mix R      | 0–100 %    | 30 %    | dry/wet, right channel (independent)               |
| Feedback   | 0–95 %     | 35 %    | pre-reverb feedback (reverb tails don't recirculate) |
| Time L     | 20–2000 ms | 375 ms  | left-channel delay time (log scale)                |
| Time R     | 20–2000 ms | 500 ms  | right-channel delay time (independent)             |
| Mod Rate   | 0.05–10 Hz | 0.8 Hz  | LFO speed (log scale)                              |
| Mod Depth  | 0–30 ms    | 4 ms    | delay-time deviation                               |
| Rev Decay  | 0.2–10 s   | 3 s     | hall decay, mapped to RT60 (log scale)             |
| Rev Mix    | 0–100 %    | 35 %    | reverb amount on the repeats                       |

The two channels' LFOs are in anti-phase (half-period offset) for a wider
stereo image. The delay line is read with cubic interpolation.

## Architecture

```
Source/DSP/DelayHallEngine.{h,cpp}   DSP core in plain C++, no JUCE dependency
Source/PluginProcessor.{h,cpp}       AudioProcessor + parameters (APVTS) + state
Source/PluginEditor.{h,cpp}          GUI: 8 rotary knobs
tests/dsp_test.cpp                   DSP unit tests, runnable anywhere
CMakeLists.txt                       CMake project (JUCE via FetchContent)
```

Signal flow (per sample, per channel):

```
input ──┬──────────────────────────────► dry ─┐
        │                                      ├─► outL = (1-mixL)*dryL + mixL*wetL
        └─► [delay line + feedback] ─► d ─────┴─► outR = (1-mixR)*dryR + mixR*wetR
                                    │               wet = d + revMix * hall(d)
                                    └─► hall reverb (repeats only)
```

## Quick setup on macOS (one command)

```sh
./setup.sh
```

Run from the repository root: it configures CMake, builds VST3 + AU +
Standalone, runs the DSP unit tests, signs the bundles (ad-hoc) and installs
them into `~/Library/Audio/Plug-Ins/`, **overwriting any previous install**.
Then restart your DAW (or run `killall -9 AudioComponentRegistrar`).

## Manual build on Mac (produces VST3 + AU + Standalone)

Prerequisites: **Xcode** (with Command Line Tools) and **CMake** (`brew install cmake`).

```sh
cd StereoDelayHall   # or the root of this repository
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artifacts land in `build/StereoDelayHall_artefacts/Release/`, one subfolder per format:

- `VST3/StereoDelayHall.vst3` → copy to `~/Library/Audio/Plug-Ins/VST3/`
- `AU/StereoDelayHall.component` → copy to `~/Library/Audio/Plug-Ins/Components/`
- `Standalone/StereoDelayHall.app` → standalone app to try it immediately (no DAW needed)

To use a different JUCE version: `cmake -B build -DJUCE_GIT_TAG=8.0.4`
(the tag `8.0.4` is pinned in `CMakeLists.txt`).

Distributing an AU/VST3 on macOS requires signing and notarization
(see JUCE's AU packaging docs); for local personal use it is not needed.

## Running the DSP tests (no JUCE required, any platform)

```sh
g++ -std=c++17 -O2 -I Source tests/dsp_test.cpp Source/DSP/DelayHallEngine.cpp -o dsp_test
./dsp_test
```

or via CMake: `cmake --build build --target dsp_test && ./build/dsp_test`

The tests verify: exact dry passthrough at mix=0 (the reverb never receives the
dry signal), independent L/R repeat positions, repeat amplitudes with
feedback, reverb tail present only after the repeats, repeat shift under
modulation, no L/R crossfeed, stability at extreme settings (no NaN),
operation at 48 kHz.

## License

The code in this repository is yours, but **JUCE is dual-licensed**: AGPLv3 or
the commercial JUCE licence (https://juce.com/get-juce/). If you distribute the
plugin closed-source/commercially without complying with the AGPLv3, you need a
commercial JUCE licence. For personal use or AGPLv3 open-source projects, no
further licence is needed.
