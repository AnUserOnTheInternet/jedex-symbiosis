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
   track's routing arrow → **Sidechain to this track**
   (*not* "…to this track **only**", or the priority track stops reaching the
   master and you will not hear it any more).
3. **This step is mandatory and easy to miss.** Open the CARVE wrapper menu →
   **Processing → Connections**, and on the row **2. Priority A (Sidechain)**
   drag the small box upward until it reads **1**. Until you do, FL feeds the
   plugin its *own* track audio on that input — CARVE ends up measuring itself,
   auto-calibration correctly finds nothing to fix, and the depth sits at 0 %.
   CARVE detects exactly this and shows **SIDECHAIN NOT ROUTED** in red, so you
   never have to guess. A second source can go to **Priority B** the same way.
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
