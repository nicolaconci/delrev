# essentia

*(shown as **essentia** in DAWs; file/target names remain `StereoDelayHall.*`)*

Audio plugin (**VST3 + Audio Unit on macOS**, plus a Standalone app) — a
**stereo delay** with independent left/right delay times, time modulation,
per-channel feedback and mix, an attack fade-in per repeat, and a **hall
reverb fed only by the delay repeats** (never by the dry signal): every repeat
gets its own reverb tail. Works on any source — guitar, vocals, synths, buses.

## Features

- Independent **Time L / Time R** (20–2000 ms), **Feedback L / R**, **Mix L / R**
- **Pre Gain** (input) and **Master** (output), in dB
- **Modulation** (Rate + Depth) on the delay time, anti-phase L/R for width
- **Attack** fade-in per repeat (amplitude envelope, body preserved),
  with **Linear / Log / Exp** ramp curve
- **Hall reverb on the repeats only**, with RT60-mapped **Decay** and **Mix**
- **100% Wet** toggle — forces Mix L/R to 100% for aux bus/send use
- Bus layouts: stereo→stereo, **mono→stereo** (mono insert widens to stereo),
  stereo→mono, mono→mono
- Full parameter automation + state save/restore (APVTS)

The UI is grouped into colour-coded sections — row 1: `PRE | LEFT | RIGHT |
MASTER`, row 2: `SMEAR | MOD | REV | WET` (orange / blue / green / grey /
purple / teal).

## Parameters

| Section | Parameter       | Range             | Default | Notes                                                          |
|---------|-----------------|-------------------|---------|----------------------------------------------------------------|
| PRE     | Pre Gain        | -24..+24 dB       | 0 dB    | input gain, before dry/delay/feedback                          |
| LEFT    | Time L          | 20–2000 ms        | 375 ms  | left delay time (log scale)                                    |
| LEFT    | Feedback L      | 0–95 %            | 35 %    | left feedback (recirculating, pre-reverb)                      |
| LEFT    | Tone L          | 0–100 %           | 100 %   | left wet lowpass: 100% bright (~18 kHz), 0% dark (~300 Hz)     |
| LEFT    | Mix L           | 0–100 %           | 30 %    | left dry/wet                                                   |
| RIGHT   | Time R          | 20–2000 ms        | 500 ms  | right delay time (independent)                                 |
| RIGHT   | Feedback R      | 0–95 %            | 35 %    | right feedback (independent)                                   |
| RIGHT   | Tone R          | 0–100 %           | 100 %   | right wet lowpass (independent — e.g. one bright, one dark)   |
| RIGHT   | Mix R           | 0–100 %           | 30 %    | right dry/wet (independent)                                    |
| MASTER  | Master          | -48..+12 dB       | 0 dB    | output volume                                                  |
| SMEAR   | Smear | Crystal | -100..+100 %      | 0 %     | bipolar, applied once to the delay-line input: center = off; CCW = smear (lowpass, log curve, acts early); CW = crystal (blend toward highpass, capped at 2.5 kHz so the repeat is never silenced). First repeat affected, later repeats inherit it — no compounding |
| SMEAR   | Swell           | 0–500 ms          | 0 ms    | volume swell-in per repeat (time ramp on the delay-line input)  |
| SMEAR   | On Input (toggle) | on/off          | off     | also apply the swell to the dry input signal, not just the repeats |
| MOD     | Rate            | 0.05–10 Hz        | 0.8 Hz  | LFO speed (log scale), anti-phase L/R                          |
| MOD     | Depth           | 0–30 ms           | 4 ms    | delay-time deviation                                           |
| REV     | Decay           | 0.2–10 s          | 3 s     | hall decay, RT60-mapped (log scale)                            |
| REV     | Mix             | 0–100 %           | 35 %    | reverb amount on the repeats only                              |
| REV     | Width           | 0–200 %           | 100 %   | reverb tail width (M/S): 0% mono, 100% full stereo, 200% extra wide |
| WET     | Wet Gain        | -12..+12 dB       | 0 dB    | gain on the wet path only — raise it on aux buses at 100% wet    |
| WET     | 100% Wet (button) | on/off          | off     | forces Mix L/R to 100% — aux bus/send (wet-only), knobs untouched |

The two channels' LFOs are in anti-phase (half-period offset) for a wider
stereo image. The delay line is read with cubic interpolation.

## Bus layouts

- **stereo → stereo** (normal case)
- **mono → stereo**: on a mono-track insert the same input feeds both delay
  channels, and the independent L/R times widen it to stereo. If the host
  instead upmixes the mono track to a stereo bus with the signal on the left
  channel only (right input exactly silent), the plugin detects it and
  duplicates the left channel, so the widening works in insert too
- **stereo → mono**: mixdown (L+R)/2
- **mono → mono**

## Architecture

```
CMakeLists.txt                     CMake project; JUCE 8.0.4 fetched via FetchContent
setup.sh                           one-command build + sign + install (macOS)
Source/DSP/DelayHallEngine.{h,cpp} DSP core in plain C++ (no JUCE dependency, unit-testable)
Source/PluginProcessor.{h,cpp}     AudioProcessor, 14 parameters (APVTS), bus layouts, state
Source/PluginEditor.{h,cpp}        GUI: sectioned, colour-coded controls
Source/LookAndFeel/StudioLookAndFeel.h   hardware-style LookAndFeel (procedural knobs, metal toggles, brushed panel)
tests/dsp_test.cpp                 30 DSP unit tests, runnable without JUCE
```

Signal flow (per sample, per channel):

```
input ─► [pre gain] ─┬────────────────► dry ─┐
                     │                       ├─► outL = ((1-mixL)*dryL + mixL*wetL) * master
                     └─► [delay line + feedbackL/R] ─► d ─► [attack envelope] ─► w
                                     │                ├─► outR = ((1-mixR)*dryR + mixR*wetR) * master
                                     │                │   wet = w + revMix * hall(w)
                                     └─► hall reverb (repeats only)
```

## Dependencies

- **macOS** with **Xcode** (incl. Command Line Tools: `xcode-select --install`)
- **CMake ≥ 3.22** (`brew install cmake`)
- **JUCE 8.0.4**, fetched automatically by CMake (`FetchContent`, tag pinned in
  `CMakeLists.txt`; override with `-DJUCE_GIT_TAG=...`). Requires network on
  first configure.
- C++17 compiler (clang from Xcode)
- JUCE is **dual-licensed AGPLv3 / commercial** — see License below

## Quick setup on macOS (one command)

```sh
git clone https://github.com/nicolaconci/delrev.git   # or: gh repo clone nicolaconci/delrev
cd delrev
./setup.sh
```

`setup.sh` (run from the repo root) checks prerequisites, configures CMake,
builds VST3 + AU + Standalone, runs the DSP unit tests, signs the bundles
(ad-hoc `codesign`) and installs them into `~/Library/Audio/Plug-Ins/`,
**overwriting any previous install**. Then restart the DAW (or
`killall -9 AudioComponentRegistrar`).

## Installer (.pkg, macOS)

```sh
./setup.sh        # build first
./installer.sh    # creates build/essentia-1.0.0.pkg
open build/essentia-1.0.0.pkg
```

The installer puts the VST3 and AU into `/Library/Audio/Plug-Ins/` (asks for the
admin password) and rescans Audio Units on install. **For local/personal use no
Apple verification is needed** (ad-hoc signature). To distribute publicly over
the internet you must sign with an Apple Developer ID and notarize
(hardened runtime, $99/yr developer account), otherwise Gatekeeper blocks it.

## Manual build (produces VST3 + AU + Standalone)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# build a single format:
#   cmake --build build --config Release --target StereoDelayHall_VST3
#   cmake --build build --config Release --target StereoDelayHall_AU
#   cmake --build build --config Release --target StereoDelayHall_Standalone
# universal binary (arm64 + x86_64):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

Artifacts land in `build/StereoDelayHall_artefacts/Release/`, one subfolder per
format: `VST3/StereoDelayHall.vst3`, `AU/StereoDelayHall.component`,
`Standalone/StereoDelayHall.app`.

## Install

```sh
cd build/StereoDelayHall_artefacts/Release
codesign --force --deep --sign - VST3/StereoDelayHall.vst3
codesign --force --deep --sign - AU/StereoDelayHall.component
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
cp -R VST3/StereoDelayHall.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R AU/StereoDelayHall.component ~/Library/Audio/Plug-Ins/Components/
```

Restart the DAW and rescan. (Most DAWs also load the bundles unsigned; ad-hoc
signing avoids Gatekeeper issues. Distributing an AU/VST3 requires signing +
notarization — not needed for local use.)

## Standalone app and microphone (macOS)

To hear the plugin live (e.g. with the Mac's built-in mic) in the Standalone app:

1. System Settings → Privacy & Security → **Microphone** → enable **StereoDelayHall** (the app asks on first use)
2. System Settings → Sound → Input: select **Built-in Microphone** and check the input level moves
3. In the Standalone window: **Audio Settings** (speaker icon, top right) → Output: Built-in Output, Input: Built-in Microphone, tick the input channel checkboxes (In 1 / In 2)
4. Speak: with Mix ~50% you hear your voice through the delay

## Running the DSP tests (no JUCE needed, any platform)

```sh
g++ -std=c++17 -O2 -I Source tests/dsp_test.cpp Source/DSP/DelayHallEngine.cpp -o dsp_test
./dsp_test
# or via CMake:
cmake --build build --target dsp_test && ./build/dsp_test
```

30 checks covering: exact dry passthrough at mix=0 (reverb never receives dry),
independent L/R repeat positions and amplitudes, reverb tail only after the
repeats, modulation shift, no L/R crossfeed, pre-gain/master scaling,
attack fade-in ramp and curves, mono→stereo widening, stability at extreme
settings (no NaN), 48 kHz operation.

## License

The code in this repository is yours, but **JUCE is dual-licensed**: AGPLv3 or
the commercial JUCE licence (https://juce.com/get-juce/). If you distribute the
plugin closed-source/commercially without complying with the AGPLv3, you need a
commercial JUCE licence. For personal use or AGPLv3 open-source projects, no
further licence is needed.
