#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

//==============================================================================
/**
    SpectralEngine — one instance per audio channel (L/R).

    Runs a 2048-point STFT (75 % overlap, periodic Hann, weighted overlap-add)
    over three time-aligned layers:

      * Main layer      : analysed only, passed through a matching delay line.
      * Sidechain A / B : re-synthesised after per-bin gain carving.

    The carve gain per bin is derived from a Wiener-style "collision mask"
    against the (attack/release smoothed) main-layer energy:

        mask = mainE / (mainE + sideE)        -> 1 where the main dominates
        gain = 1 - depth * mask

    Only magnitudes are attenuated — the sidechain phases are never touched,
    so the carving itself introduces no phase distortion.

    Latency is exactly fftSize samples on all three paths and is reported to
    the host, so FL Studio's PDC keeps everything sample-aligned.
*/
class SpectralEngine
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;      // 2048
    static constexpr int hopSize  = fftSize / 4;        // 512 -> 75 % overlap
    static constexpr int numBins  = fftSize / 2 + 1;    // 1025

    /** Optional per-bin taps for the UI visualiser (arrays of numBins atomics,
        owned by the processor; written with relaxed stores on the audio thread). */
    struct DisplayTap
    {
        std::atomic<float>* mainMag = nullptr;
        std::atomic<float>* sideMag = nullptr;
        std::atomic<float>* carve   = nullptr;
    };

    SpectralEngine();

    void prepare (double sampleRate);
    void reset() noexcept;

    void setDepth (float newDepth) noexcept           { targetDepth = newDepth; }
    void setDisplayTap (const DisplayTap& t) noexcept { tap = t; }

    /** Push one sample of each layer, receive the three aligned outputs
        (each exactly fftSize samples late). */
    void processSample (float mainIn, float sideAIn, float sideBIn,
                        float& mainOut, float& sideAOut, float& sideBOut) noexcept;

private:
    void processFrame() noexcept;

    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> window {};

    int pos      = 0;   // shared circular write/read index
    int hopCount = 0;

    std::array<float, fftSize> mainFifo {}, sideFifoA {}, sideFifoB {};
    std::array<float, fftSize> mainDelay {};            // dry main alignment
    std::array<float, fftSize> olaA {}, olaB {};        // overlap-add accumulators

    std::array<float, fftSize * 2> mainFrame {}, frameA {}, frameB {};

    std::array<float, numBins> mainEnergy {};           // smoothed main energy
    std::array<float, numBins> gainA {}, gainB {};      // time-smoothed gains
    std::array<float, numBins> targetA {}, targetB {};
    std::array<float, numBins> smoothA {}, smoothB {};  // frequency-smoothed gains

    float depth = 0.0f, targetDepth = 0.0f;
    float energyAttack = 0.6f, energyRelease = 0.12f;
    float gainAttack   = 0.5f, gainRelease   = 0.12f;

    DisplayTap tap;
};

//==============================================================================
class JeDExSymbiosisAudioProcessor : public juce::AudioProcessor
{
public:
    JeDExSymbiosisAudioProcessor();
    ~JeDExSymbiosisAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                            { return true; }

    //==========================================================================
    const juce::String getName() const override    { return "JeDEx Symbiosis"; }
    bool acceptsMidi() const override              { return false; }
    bool producesMidi() const override             { return false; }
    bool isMidiEffect() const override             { return false; }
    double getTailLengthSeconds() const override   { return 0.0; }

    int getNumPrograms() override                  { return 1; }
    int getCurrentProgram() override               { return 0; }
    void setCurrentProgram (int) override          {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    static constexpr int kNumBins = SpectralEngine::numBins;

    // Visualiser taps (left-channel analysis), read by the editor at ~30 Hz.
    std::array<std::atomic<float>, kNumBins> displayMain;
    std::array<std::atomic<float>, kNumBins> displaySide;
    std::array<std::atomic<float>, kNumBins> displayCarve;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* symbiosisParam = nullptr;
    SpectralEngine engines[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JeDExSymbiosisAudioProcessor)
};
