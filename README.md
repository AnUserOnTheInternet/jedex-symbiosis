# JeDEx CARVE

**Context-aware harmonic carving** — put CARVE on any track or bus that should
*make room*, feed it your priority sound (lead, vocal, kick...) through the
sidechain, and it dynamically carves exactly the colliding frequencies out of
the material it sits on. 1025-band spectral resolution, phase-untouched,
zero manual EQ.

## How to use (FL Studio)

1. Put **JeDEx CARVE** on the mixer track/bus you want to duck spectrally
   (e.g. the bus with all your layers, or the instrumental under a vocal).
2. Select the priority track (your lead/vocal/kick), right-click the target
   track's routing arrow → **Sidechain to this track**.
3. Open the CARVE wrapper → **Processing → Connections** and map the
   sidechain to **Priority A**. (A second source can go to **Priority B**.)
4. The status pill switches to **CARVING** and the spectrum shows the bite.

Controls: **Amount** (carve depth — at 100 % the carve is a true Wiener
separation filter), **Smoothness** (release/character), **Mix** (parallel
blend), **Output** (makeup gain), **Eco Mode** (halves CPU and latency for
weaker machines: 1024-band engine + lighter graphics).

Latency: 2048 samples (1024 in Eco), reported to the host — PDC keeps
everything aligned.

## Building

CMake + JUCE 8.0.4 (auto-downloaded). Push to GitHub and the included
workflow builds the VST3 and publishes it as the `latest` release
(`JeDEx-CARVE-VST3.zip`). Local build: VS2022 Build Tools + CMake:

```
cmake -B build
cmake --build build --config Release
```

Output: `build/Carve_artefacts/Release/VST3/JeDEx CARVE.vst3` →
copy to `C:\Program Files\Common Files\VST3`.

## Credits

By **JeDEx** × **Big Ice** — logos and Spotify links are embedded in the
plugin's Credits panel.
