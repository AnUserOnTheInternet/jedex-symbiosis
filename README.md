# JeDEx Symbiosis

**Dynamic Harmonic Carving for Lead Layering** — a VST3 processor that takes a
main lead layer plus two secondary layers (via sidechain) and automatically
carves the colliding harmonics out of the secondary layers, in real time, so
the composite lead stays clean with zero manual EQ.

## How it works

```
 Main In ────────┐
                 │   2048-pt STFT (75% overlap, Hann, WOLA)
 Sidechain A ──▶ │   per-bin Wiener-style collision mask:
 Sidechain B ──▶ │     mask = mainE / (mainE + sideE)
                 │     gain = 1 - depth * mask       (magnitude only,
                 │                                    phases untouched)
 Output ◀── main (delay-aligned) + carved A + carved B
```

* At **100 % Symbiosis Amount**, each sidechain bin is filtered by its
  theoretical least-squares (Wiener) separation gain against the main layer.
* Attack/release smoothing over time + 3-tap smoothing across frequency
  suppress musical-noise artifacts.
* Latency is exactly **2048 samples** (≈ 43 ms @ 48 kHz), reported to the host
  — FL Studio's plugin delay compensation keeps everything aligned during
  playback.

## FL Studio setup

1. Put **JeDEx Symbiosis** on the mixer track of your **main** lead layer.
2. For each secondary layer's mixer track: right-click the **arrow at the
   bottom of the main lead's mixer track** → **Sidechain to this track**
   (this routes audio to the plugin without it also playing through master).
3. Open the plugin wrapper → **gear icon → Processing tab → Connections**, and
   map the two sidechain sources to input buses **Layer B** / **Layer C**.
4. Turn the big knob.

## Building

The project is CMake-based and downloads JUCE 8.0.4 automatically
(FetchContent) — no manual SDK installs.

### Option A — GitHub Actions (no local tools or disk needed) ✅

1. Create a GitHub repository and push this folder.
2. The included workflow (`.github/workflows/build-vst3.yml`) builds the
   plugin on GitHub's servers on every push (free for public repos).
3. Download the `JeDEx-Symbiosis-VST3-Windows` artifact from the Actions tab,
   unzip, and copy `JeDEx Symbiosis.vst3` to
   `C:\Program Files\Common Files\VST3`.

### Option B — Local build (needs ~8 GB of tools)

Requires Visual Studio 2022 Build Tools (C++ workload) + CMake:

```
cmake -B build
cmake --build build --config Release
```

Output: `build/JeDExSymbiosis_artefacts/Release/VST3/JeDEx Symbiosis.vst3`
