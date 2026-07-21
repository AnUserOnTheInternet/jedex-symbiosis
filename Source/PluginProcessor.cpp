#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// SpectralEngine
//==============================================================================
SpectralEngine::SpectralEngine()
{
    // Periodic Hann (denominator = fftSize) for exact COLA at 75 % overlap.
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                       * (float) i / (float) fftSize));
    reset();
}

void SpectralEngine::prepare (double sampleRate)
{
    // One-pole coefficients evaluated once per STFT frame (hopSize samples).
    auto onePolePerFrame = [sampleRate] (double seconds)
    {
        return 1.0f - (float) std::exp (-(double) hopSize / (sampleRate * seconds));
    };

    energyAttack  = onePolePerFrame (0.003);
    energyRelease = onePolePerFrame (0.060);
    gainAttack    = onePolePerFrame (0.005);
    gainRelease   = onePolePerFrame (0.100);

    reset();
}

void SpectralEngine::reset() noexcept
{
    pos = 0;
    hopCount = 0;

    mainFifo.fill (0.0f);   sideFifoA.fill (0.0f);  sideFifoB.fill (0.0f);
    mainDelay.fill (0.0f);  olaA.fill (0.0f);       olaB.fill (0.0f);
    mainEnergy.fill (0.0f);
    gainA.fill (1.0f);      gainB.fill (1.0f);

    depth = targetDepth;
}

void SpectralEngine::processSample (float mainIn, float sideAIn, float sideBIn,
                                    float& mainOut, float& sideAOut, float& sideBOut) noexcept
{
    // Read the three aligned outputs first (each exactly fftSize samples late),
    // then push the new inputs into the freed slot.
    mainOut  = mainDelay[(size_t) pos];
    sideAOut = olaA[(size_t) pos];
    sideBOut = olaB[(size_t) pos];

    olaA[(size_t) pos] = 0.0f;
    olaB[(size_t) pos] = 0.0f;

    mainDelay[(size_t) pos] = mainIn;
    mainFifo[(size_t) pos]  = mainIn;
    sideFifoA[(size_t) pos] = sideAIn;
    sideFifoB[(size_t) pos] = sideBIn;

    pos = (pos + 1) & (fftSize - 1);

    if (++hopCount >= hopSize)
    {
        hopCount = 0;
        processFrame();
    }
}

void SpectralEngine::processFrame() noexcept
{
    depth += 0.25f * (targetDepth - depth);

    // 1) Unroll the circular FIFOs into chronological, Hann-windowed frames.
    //    After the increment in processSample, fifo[pos] is the oldest sample.
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (pos + i) & (fftSize - 1);
        const float w = window[(size_t) i];
        mainFrame[(size_t) i] = mainFifo[(size_t) idx]  * w;
        frameA[(size_t) i]    = sideFifoA[(size_t) idx] * w;
        frameB[(size_t) i]    = sideFifoB[(size_t) idx] * w;
    }
    std::fill (mainFrame.begin() + fftSize, mainFrame.end(), 0.0f);
    std::fill (frameA.begin()    + fftSize, frameA.end(),    0.0f);
    std::fill (frameB.begin()    + fftSize, frameB.end(),    0.0f);

    // 2) Forward FFTs. The main layer is analysis-only.
    fft.performRealOnlyForwardTransform (mainFrame.data(), true);
    fft.performRealOnlyForwardTransform (frameA.data());
    fft.performRealOnlyForwardTransform (frameB.data());

    // 3) Per-bin collision masks -> target carve gains.
    constexpr float eps = 1.0e-12f;
    const bool hasTap = (tap.mainMag != nullptr);

    for (int k = 0; k < numBins; ++k)
    {
        const float mre = mainFrame[(size_t) (2 * k)];
        const float mim = mainFrame[(size_t) (2 * k + 1)];
        const float mE  = mre * mre + mim * mim;

        float& ms = mainEnergy[(size_t) k];
        ms += (mE > ms ? energyAttack : energyRelease) * (mE - ms);

        const float are = frameA[(size_t) (2 * k)];
        const float aim = frameA[(size_t) (2 * k + 1)];
        const float aE  = are * are + aim * aim;

        const float bre = frameB[(size_t) (2 * k)];
        const float bim = frameB[(size_t) (2 * k + 1)];
        const float bE  = bre * bre + bim * bim;

        // Wiener-style mask: -> 1 where the main layer dominates this bin,
        // -> 0 where the sidechain owns spectrum the main doesn't need.
        const float maskA = ms / (ms + aE + eps);
        const float maskB = ms / (ms + bE + eps);

        targetA[(size_t) k] = 1.0f - depth * maskA;
        targetB[(size_t) k] = 1.0f - depth * maskB;

        if (hasTap)
        {
            tap.mainMag[k].store (std::sqrt (ms), std::memory_order_relaxed);
            tap.sideMag[k].store (std::sqrt (aE) + std::sqrt (bE), std::memory_order_relaxed);
        }
    }

    // 4) Attack/release smoothing over time (fast carve-in, slower release)...
    for (int k = 0; k < numBins; ++k)
    {
        float& gA = gainA[(size_t) k];
        float& gB = gainB[(size_t) k];
        const float tA = targetA[(size_t) k];
        const float tB = targetB[(size_t) k];
        gA += (tA < gA ? gainAttack : gainRelease) * (tA - gA);
        gB += (tB < gB ? gainAttack : gainRelease) * (tB - gB);
    }

    // ...plus a light 3-tap smoothing across frequency (suppresses musical noise).
    auto smoothSpectrum = [] (const std::array<float, numBins>& in,
                              std::array<float, numBins>& out)
    {
        out[0] = in[0];
        out[(size_t) (numBins - 1)] = in[(size_t) (numBins - 1)];
        for (int k = 1; k < numBins - 1; ++k)
            out[(size_t) k] = 0.25f * in[(size_t) (k - 1)]
                            + 0.50f * in[(size_t) k]
                            + 0.25f * in[(size_t) (k + 1)];
    };
    smoothSpectrum (gainA, smoothA);
    smoothSpectrum (gainB, smoothB);

    // 5) Apply the gains (magnitude only — phases untouched)...
    for (int k = 0; k < numBins; ++k)
    {
        const float gA = smoothA[(size_t) k];
        const float gB = smoothB[(size_t) k];

        frameA[(size_t) (2 * k)]     *= gA;
        frameA[(size_t) (2 * k + 1)] *= gA;
        frameB[(size_t) (2 * k)]     *= gB;
        frameB[(size_t) (2 * k + 1)] *= gB;

        if (hasTap)
            tap.carve[k].store (1.0f - juce::jmin (gA, gB), std::memory_order_relaxed);
    }

    // ...and rebuild the conjugate-symmetric negative-frequency half so the
    // real-only inverse transform sees a consistent spectrum.
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const int m = fftSize - k;
        frameA[(size_t) (2 * m)]     =  frameA[(size_t) (2 * k)];
        frameA[(size_t) (2 * m + 1)] = -frameA[(size_t) (2 * k + 1)];
        frameB[(size_t) (2 * m)]     =  frameB[(size_t) (2 * k)];
        frameB[(size_t) (2 * m + 1)] = -frameB[(size_t) (2 * k + 1)];
    }

    // 6) Back to time domain, synthesis window + COLA-normalised overlap-add.
    fft.performRealOnlyInverseTransform (frameA.data());
    fft.performRealOnlyInverseTransform (frameB.data());

    constexpr float colaNorm = 2.0f / 3.0f;   // sum of hann^2 at 75 % overlap = 1.5
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (pos + i) & (fftSize - 1);
        const float w = window[(size_t) i] * colaNorm;
        olaA[(size_t) idx] += frameA[(size_t) i] * w;
        olaB[(size_t) idx] += frameB[(size_t) i] * w;
    }
}

//==============================================================================
// JeDExSymbiosisAudioProcessor
//==============================================================================
JeDExSymbiosisAudioProcessor::JeDExSymbiosisAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Main In",              juce::AudioChannelSet::stereo(), true)
          .withInput  ("Layer B (Sidechain)",  juce::AudioChannelSet::stereo(), true)
          .withInput  ("Layer C (Sidechain)",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output",               juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    symbiosisParam = apvts.getRawParameterValue ("symbiosis");

    for (auto& v : displayMain)  v.store (0.0f);
    for (auto& v : displaySide)  v.store (0.0f);
    for (auto& v : displayCarve) v.store (0.0f);

    // Only the left-channel engine feeds the visualiser.
    engines[0].setDisplayTap ({ displayMain.data(), displaySide.data(), displayCarve.data() });
}

juce::AudioProcessorValueTreeState::ParameterLayout
JeDExSymbiosisAudioProcessor::createParameterLayout()
{
    return { std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "symbiosis", 1 },
        "Symbiosis Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f),
        0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })) };
}

void JeDExSymbiosisAudioProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& e : engines)
    {
        e.setDepth (symbiosisParam->load());
        e.prepare (sampleRate);
    }

    setLatencySamples (SpectralEngine::fftSize);
}

bool JeDExSymbiosisAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet()  != juce::AudioChannelSet::stereo()
     || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Sidechains may be stereo, mono, or left unconnected.
    for (int i = 1; i < layouts.inputBuses.size(); ++i)
    {
        const auto& set = layouts.getChannelSet (true, i);
        if (! set.isDisabled()
             && set != juce::AudioChannelSet::stereo()
             && set != juce::AudioChannelSet::mono())
            return false;
    }

    return true;
}

void JeDExSymbiosisAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                 juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int numSamples = buffer.getNumSamples();

    const float depthNow = symbiosisParam->load();
    engines[0].setDepth (depthNow);
    engines[1].setDepth (depthNow);

    auto mainIn = getBusBuffer (buffer, true, 0);
    if (mainIn.getNumChannels() == 0)
        return;

    const float* mL = mainIn.getReadPointer (0);
    const float* mR = mainIn.getNumChannels() > 1 ? mainIn.getReadPointer (1) : mL;

    // Sidechain buses may be missing/disabled — fall back to silence.
    auto sidePointers = [this, &buffer] (int busIndex, const float*& l, const float*& r)
    {
        l = r = nullptr;

        if (busIndex < getBusCount (true))
        {
            if (auto* bus = getBus (true, busIndex); bus != nullptr && bus->isEnabled())
            {
                auto sc = getBusBuffer (buffer, true, busIndex);
                if (sc.getNumChannels() > 0)
                {
                    l = sc.getReadPointer (0);
                    r = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : l;
                }
            }
        }
    };

    const float *aL, *aR, *bL, *bR;
    sidePointers (1, aL, aR);
    sidePointers (2, bL, bR);

    auto mainOut = getBusBuffer (buffer, false, 0);
    float* oL = mainOut.getWritePointer (0);
    float* oR = mainOut.getNumChannels() > 1 ? mainOut.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float m, a, b;

        // Read all inputs for this sample index before writing the (aliased)
        // output channels.
        engines[0].processSample (mL[i], aL != nullptr ? aL[i] : 0.0f,
                                         bL != nullptr ? bL[i] : 0.0f, m, a, b);
        const float left = m + a + b;

        engines[1].processSample (mR[i], aR != nullptr ? aR[i] : 0.0f,
                                         bR != nullptr ? bR[i] : 0.0f, m, a, b);
        const float right = m + a + b;

        oL[i] = left;
        if (oR != nullptr)
            oR[i] = right;
    }
}

//==============================================================================
juce::AudioProcessorEditor* JeDExSymbiosisAudioProcessor::createEditor()
{
    return new JeDExSymbiosisAudioProcessorEditor (*this);
}

void JeDExSymbiosisAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void JeDExSymbiosisAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JeDExSymbiosisAudioProcessor();
}
