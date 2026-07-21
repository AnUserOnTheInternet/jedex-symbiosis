#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

//==============================================================================
/**
    CarveEngine — one instance per audio channel (L/R).

    Spectral "make room" processor. The MAIN input is the material that gets
    carved (a layer bus, a pad stack, a full instrumental...). The two
    sidechain inputs are PRIORITY references (lead, vocal, kick...) that are
    analysed only. Wherever a priority source needs spectrum, the same bands
    are dynamically attenuated in the main input — magnitudes only, phases
    untouched.

    STFT: 2048-point (1024 in Eco mode), 75 % overlap, periodic Hann, weighted
    overlap-add. Latency = fftSize samples, reported to the host.

    Per bin:  mask = refE / (refE + mainE)   (Wiener-style, ->1 where the
    priority source dominates), gain = 1 - depth * mask, with attack/release
    and spectral smoothing against musical noise.

    CPU design for weak machines:
      * reference FFTs are skipped entirely while that sidechain is silent or
        disconnected (block-level gate with hold, driven by the processor);
      * the main resynthesis chain always runs, so idle<->active transitions
        are glitch-free;
      * the two channel engines are hop-staggered so their FFT bursts never
        land in the same audio callback;
      * windowing / overlap-add use SIMD (FloatVectorOperations).
*/
class CarveEngine
{
public:
    static constexpr int maxOrder   = 11;
    static constexpr int maxFftSize = 1 << maxOrder;        // 2048
    static constexpr int maxBins    = maxFftSize / 2 + 1;   // 1025

    struct DisplayTap
    {
        std::atomic<float>* refMag  = nullptr;   // priority spectrum (combined)
        std::atomic<float>* mainMag = nullptr;   // main content, pre-carve
        std::atomic<float>* carve   = nullptr;   // 1 - gain per bin
    };

    CarveEngine() = default;

    void prepare (double sampleRate, int fftOrder, bool staggerHalfHop);
    void setOrder (int newOrder);                 // audio thread, between blocks
    void reset() noexcept;

    void setDepth (float d) noexcept                    { targetDepth = d; }
    void setSmoothness (float s) noexcept               { smoothness = s; }
    void setRefsActive (bool a, bool b) noexcept        { refAOn = a; refBOn = b; }
    void setDisplayTap (const DisplayTap& t) noexcept   { tap = t; }

    /** Average linear gain applied to the main energy in the last frame (1 = no carving). */
    float getCarvedGainLin() const noexcept             { return carvedGainLin; }

    /** dryOut = delay-aligned input, wetOut = carved resynthesis (same latency). */
    void processSample (float mainIn, float refAIn, float refBIn,
                        float& dryOut, float& wetOut) noexcept;

private:
    void applyOrder (int newOrder);
    void processFrame() noexcept;

    juce::dsp::FFT fftBig { maxOrder };
    juce::dsp::FFT fftEco { maxOrder - 1 };
    juce::dsp::FFT* fft = &fftBig;

    double sr = 48000.0;
    int order   = maxOrder;
    int fftSize = maxFftSize;
    int hopSize = maxFftSize / 4;
    int numBins = maxBins;
    bool stagger = false;

    std::array<float, maxFftSize> window {}, synthWindow {};

    int pos = 0, hopCount = 0;

    std::array<float, maxFftSize> mainFifo {}, refFifoA {}, refFifoB {};
    std::array<float, maxFftSize> mainDelay {}, ola {};

    std::array<float, maxFftSize * 2> mainFrame {}, refFrame {};
    std::array<float, maxFftSize> scratch {};

    std::array<float, maxBins> refEAccum {}, refEnergySm {};
    std::array<float, maxBins> gain {}, gTarget {}, gSmooth {};

    float depth = 0.0f, targetDepth = 0.0f, smoothness = 0.5f;
    bool refAOn = false, refBOn = false;
    float engAtk = 0.6f, engRel = 0.12f, gAtk = 0.5f;
    float carvedGainLin = 1.0f;

    DisplayTap tap;
};

//==============================================================================
class CarveAudioProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    CarveAudioProcessor();
    ~CarveAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    //==========================================================================
    const juce::String getName() const override         { return "JeDEx CARVE"; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    static constexpr int kNumBins = CarveEngine::maxBins;

    // Sidechain / carving state for the UI:
    // 0 = no sidechain connected, 1 = connected but silent, 2 = carving.
    std::atomic<int>   uiState { 0 };
    std::atomic<float> uiCarvedDb { 0.0f };     // average gain reduction, dB (<= 0)
    std::atomic<int>   uiFftSize { CarveEngine::maxFftSize };

    // Visualiser taps (left-channel engine), read by the editor timer.
    std::array<std::atomic<float>, kNumBins> displayRef;
    std::array<std::atomic<float>, kNumBins> displayMain;
    std::array<std::atomic<float>, kNumBins> displayCarve;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Latency changes (Eco switch) are detected on the audio thread but reported
    // to the host from the message thread — setLatencySamples takes a lock and
    // posts a system message, neither of which belongs in the audio callback.
    void timerCallback() override;
    std::atomic<int> pendingLatency { -1 };

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* smoothParam = nullptr;
    std::atomic<float>* mixParam    = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* ecoParam    = nullptr;

    juce::SmoothedValue<float> mixSm, outSm;

    CarveEngine engines[2];
    int curOrder = CarveEngine::maxOrder;
    int holdA = 0, holdB = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarveAudioProcessor)
};
