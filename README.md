# StereoDelayHall

Plugin audio (VST3 + Audio Unit su macOS) per chitarra: **delay stereo** con tempi
indipendenti per canale sinistro/destro, **modulazione** del tempo di delay
(rate + depth), **feedback**, **mix** dry/wet, e un **riverbero "hall"** che viene
alimentato **solo dalle ripetute del delay** (mai dal segnale diretto): ogni
ripetuta genera quindi la propria coda di riverbero, con **decay** e **mix**
dedicati.

## Parametri

| Parametro  | Range        | Default | Note                                            |
|------------|--------------|---------|-------------------------------------------------|
| Mix        | 0–100 %      | 30 %    | dry/wet                                         |
| Feedback   | 0–95 %       | 35 %    | ricircolo pre-riverbero (le code non si ricircolano) |
| Time L     | 20–2000 ms   | 375 ms  | tempo di delay canale sinistro (scala log)     |
| Time R     | 20–2000 ms   | 500 ms  | tempo di delay canale destro (indipendente)     |
| Mod Rate   | 0.05–10 Hz   | 0.8 Hz  | velocità LFO (scala log)                        |
| Mod Depth  | 0–30 ms      | 4 ms    | escursione del tempo di delay                   |
| Rev Decay  | 0.2–10 s     | 3 s     | decadimento hall, mappato su RT60 (scala log)   |
| Rev Mix    | 0–100 %      | 35 %    | quantità di riverbero sulle ripetute            |

L'LFO dei due canali è in controfase (sfasamento di mezzo periodo) per un effetto
stereo più ampio. La lettura della linea di delay usa interpolazione cubica.

## Architettura

```
Source/DSP/DelayHallEngine.{h,cpp}   nucleo DSP in C++ puro, senza dipendenze JUCE
Source/PluginProcessor.{h,cpp}       AudioProcessor + parametri (APVTS) + state
Source/PluginEditor.{h,cpp}          GUI: 8 knob rotativi
tests/dsp_test.cpp                   test unit del DSP, eseguibili ovunque
CMakeLists.txt                       progetto CMake (JUCE via FetchContent)
```

Il flusso del segnale (per campione, per canale):

```
input ──┬──────────────────────────────► dry ─┐
        │                                      ├─► out = (1-mix)*dry + mix*wet
        └─► [linea di delay + feedback] ─► d ─┴─► wet = d + revMix * hall(d)
                                    │
                                    └─► riverbero hall (solo ripetute)
```

## Build su Mac (produce VST3 + AU + Standalone)

Prerequisiti: **Xcode** (con Command Line Tools) e **CMake** (`brew install cmake`).

```sh
cd StereoDelayHall
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Gli artefatti finiscono in `build/StereoDelayHall_artefacts/Release/`:

- `StereoDelayHall.vst3` → copialo in `~/Library/Audio/Plug-Ins/VST3/`
- `StereoDelayHall.component` → copialo in `~/Library/Audio/Plug-Ins/Components/`
- `StereoDelayHall.app` → app standalone per testarlo subito (senza DAW)

Per cambiare versione di JUCE: `cmake -B build -DJUCE_GIT_TAG=8.0.4` (è fissata la
tag `8.0.4` in `CMakeLists.txt`).

Per la **distribuzione** di un AU/VST3 su Mac, Apple richiede firma e
notarizzazione (vd. documentazione JUCE "AU validation / packaging"); per uso
personale locale non serve.

## Test del nucleo DSP (senza JUCE, qualsiasi piattaforma)

```sh
g++ -std=c++17 -O2 -I Source tests/dsp_test.cpp Source/DSP/DelayHallEngine.cpp -o dsp_test
./dsp_test
```

oppure via CMake: `cmake --build build --target dsp_test && ./build/dsp_test`

I test verificano: passthrough dry esatto a mix=0 (il riverbero non riceve mai il
dry), posizione delle ripetute L/R indipendenti, ampiezza delle ripetute con
feedback, coda di riverbero presente solo dopo le ripetute, spostamento della
ripetuta con la modulazione, assenza di crossfeed tra i canali, stabilità a
impostazioni estreme (niente NaN), funzionamento a 48 kHz.

## Licenza

Il codice di questo progetto è tuo, ma **JUCE è dual-licensed**: AGPLv3 oppure
licenza commerciale JUCE (https://juce.com/get-juce/). Se distribuisci il plugin
in forma chiusa/commerciale senza rispettare la AGPLv3, ti serve una licenza
commerciale JUCE. Per uso personale o progetti open-source AGPLv3 non serve altro.
