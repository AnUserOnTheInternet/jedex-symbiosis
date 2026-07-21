#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// CarveEngine
//==============================================================================
void CarveEngine::prepare (double sampleRate, int fftOrder, bool staggerHalfHop)
{
    sr = sampleRate;
    stagger = staggerHalfHop;
    applyOrder (fftOrder);
}

void CarveEngine::setOrder (int newOrder)
{
    if (newOrder != order)
        applyOrder (newOrder);
}

void CarveEngine::applyOrder (int newOrder)
{
    order   = juce::jlimit (maxOrder - 1, maxOrder, newOrder);
    fftSize = 1 << order;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;
    fft     = (order == maxOrder ? &fftBig : &fftEco);

    // Periodic Hann for exact COLA at 75 % overlap; hann^2 sums to 1.5.
    constexpr float colaNorm = 2.0f / 3.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                       * (float) i / (float) fftSize));
        synthWindow[(size_t) i] = window[(size_t) i] * colaNorm;
    }

    auto onePolePerFrame = [this] (double seconds)
    {
        return 1.0f - (float) std::exp (-(double) hopSize / (sr * seconds));
    };
    engAtk = onePolePerFrame (0.003);
    engRel = onePolePerFrame (0.060);
    gAtk   = onePolePerFrame (0.005);

    reset();
}

void CarveEngine::reset() noexcept
{
    pos = 0;
    hopCount = stagger ? hopSize / 2 : 0;

    mainFifo.fill (0.0f);  refFifoA.fill (0.0f);  refFifoB.fill (0.0f);
    mainDelay.fill (0.0f); ola.fill (0.0f);
    refEnergySm.fill (0.0f);
    gain.fill (1.0f);

    depth = targetDepth;
    carvedGainLin = 1.0f;
}

void CarveEngine::processSample (float mainIn, float refAIn, float refBIn,
                                 float& dryOut, float& wetOut) noexcept
{
    // Read the aligned outputs first (each exactly fftSize samples late),
    // then push the new inputs into the freed slot.
    dryOut = mainDelay[(size_t) pos];
    wetOut = ola[(size_t) pos];
    ola[(size_t) pos] = 0.0f;

    mainDelay[(size_t) pos] = mainIn;
    mainFifo[(size_t) pos]  = mainIn;
    refFifoA[(size_t) pos]  = refAIn;
    refFifoB[(size_t) pos]  = refBIn;

    pos = (pos + 1) & (fftSize - 1);

    if (++hopCount >= hopSize)
    {
        hopCount = 0;
        processFrame();
    }
}

void CarveEngine::processFrame() noexcept
{
    depth += 0.25f * (targetDepth - depth);

    // The release of the per-bin gains follows the Smoothness control.
    const float relSec = 0.04f + smoothness * 0.26f;
    const float gRel = 1.0f - (float) std::exp (-(double) hopSize / (sr * (double) relSec));

    const int n1 = fftSize - pos;   // fifo[pos] is the oldest sample

    // ---- main content: stage chronologically, window (SIMD), forward FFT ---------
    std::memcpy (scratch.data(),      mainFifo.data() + pos, sizeof (float) * (size_t) n1);
    std::memcpy (scratch.data() + n1, mainFifo.data(),       sizeof (float) * (size_t) pos);
    juce::FloatVectorOperations::multiply (mainFrame.data(), scratch.data(), window.data(), fftSize);
    juce::FloatVectorOperations::clear (mainFrame.data() + fftSize, fftSize);
    fft->performRealOnlyForwardTransform (mainFrame.data(), false);

    // ---- priority references: accumulate per-bin energy (gated when silent) ------
    juce::FloatVectorOperations::clear (refEAccum.data(), numBins);
    for (int r = 0; r < 2; ++r)
    {
        if (! (r == 0 ? refAOn : refBOn))
            continue;

        const auto& fifo = (r == 0 ? refFifoA : refFifoB);
        std::memcpy (scratch.data(),      fifo.data() + pos, sizeof (float) * (size_t) n1);
        std::memcpy (scratch.data() + n1, fifo.data(),       sizeof (float) * (size_t) pos);
        juce::FloatVectorOperations::multiply (refFrame.data(), scratch.data(), window.data(), fftSize);
        juce::FloatVectorOperations::clear (refFrame.data() + fftSize, fftSize);
        fft->performRealOnlyForwardTransform (refFrame.data(), true);

        for (int k = 0; k < numBins; ++k)
        {
            const float re = refFrame[(size_t) (2 * k)];
            const float im = refFrame[(size_t) (2 * k + 1)];
            refEAccum[(size_t) k] += re * re + im * im;
        }
    }

    // ---- per-bin Wiener-style mask -> smoothed carve gains ------------------------
    constexpr float eps = 1.0e-12f;
    const bool anyRef = refAOn || refBOn;
    const bool hasTap = (tap.mainMag != nullptr);
    float totE = eps;

    for (int k = 0; k < numBins; ++k)
    {
        const float mre = mainFrame[(size_t) (2 * k)];
        const float mim = mainFrame[(size_t) (2 * k + 1)];
        const float mE  = mre * mre + mim * mim;
        totE += mE;

        float& rs = refEnergySm[(size_t) k];
        const float rE = refEAccum[(size_t) k];
        rs += (rE > rs ? engAtk : engRel) * (rE - rs);

        const float mask = anyRef ? rs / (rs + mE + eps) : 0.0f;
        const float t = 1.0f - depth * mask;

        float& g = gain[(size_t) k];
        g += (t < g ? gAtk : gRel) * (t - g);

        if (hasTap)
        {
            tap.refMag[k].store (std::sqrt (rs), std::memory_order_relaxed);
            tap.mainMag[k].store (std::sqrt (mE), std::memory_order_relaxed);
        }
    }

    // ---- spectral smoothing of the gain curve (suppresses musical noise) ----------
    gSmooth[0] = gain[0];
    gSmooth[(size_t) (numBins - 1)] = gain[(size_t) (numBins - 1)];
    for (int k = 1; k < numBins - 1; ++k)
        gSmooth[(size_t) k] = 0.25f * gain[(size_t) (k - 1)]
                            + 0.50f * gain[(size_t) k]
                            + 0.25f * gain[(size_t) (k + 1)];

    // ---- apply (magnitude only), meter the kept energy ----------------------------
    float keptE = eps;
    for (int k = 0; k < numBins; ++k)
    {
        const float g = gSmooth[(size_t) k];
        float& re = mainFrame[(size_t) (2 * k)];
        float& im = mainFrame[(size_t) (2 * k + 1)];
        re *= g;  im *= g;
        keptE += re * re + im * im;

        if (hasTap)
            tap.carve[k].store (1.0f - g, std::memory_order_relaxed);
    }
    carvedGainLin = std::sqrt (juce::jlimit (0.0f, 1.0f, keptE / totE));

    // ---- rebuild the conjugate-symmetric negative half, inverse, WOLA -------------
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const int m = fftSize - k;
        mainFrame[(size_t) (2 * m)]     =  mainFrame[(size_t) (2 * k)];
        mainFrame[(size_t) (2 * m + 1)] = -mainFrame[(size_t) (2 * k + 1)];
    }

    fft->performRealOnlyInverseTransform (mainFrame.data());
    juce::FloatVectorOperations::multiply (mainFrame.data(), synthWindow.data(), fftSize);
    juce::FloatVectorOperations::add (ola.data() + pos, mainFrame.data(), n1);
    juce::FloatVectorOperations::add (ola.data(), mainFrame.data() + n1, pos);
}

//==============================================================================
// CarveAudioProcessor
//==============================================================================
CarveAudioProcessor::CarveAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("In",                       juce::AudioChannelSet::stereo(), true)
          .withInput  ("Priority A (Sidechain)",   juce::AudioChannelSet::stereo(), true)
          .withInput  ("Priority B (Sidechain)",   juce::AudioChannelSet::stereo(), true)
          .withOutput ("Out",                      juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    amountParam = apvts.getRawParameterValue ("amount");
    smoothParam = apvts.getRawParameterValue ("smooth");
    mixParam    = apvts.getRawParameterValue ("mix");
    outputParam = apvts.getRawParameterValue ("output");
    ecoParam    = apvts.getRawParameterValue ("eco");

    for (auto& v : displayRef)   v.store (0.0f);
    for (auto& v : displayMain)  v.store (0.0f);
    for (auto& v : displayCarve) v.store (0.0f);

    engines[0].setDisplayTap ({ displayRef.data(), displayMain.data(), displayCarve.data() });

    startTimerHz (10);
}

CarveAudioProcessor::~CarveAudioProcessor()
{
    stopTimer();
}

void CarveAudioProcessor::timerCallback()
{
    const int pending = pendingLatency.exchange (-1);
    if (pending > 0)
        setLatencySamples (pending);
}

juce::AudioProcessorValueTreeState::ParameterLayout
CarveAudioProcessor::createParameterLayout()
{
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    return {
        std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "amount", 1 }, "Amount",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct),
        std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "smooth", 1 }, "Smoothness",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct),
        std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mix", 1 }, "Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct),
        std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "output", 1 }, "Output",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (v, 1) + " dB"; })),
        std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "eco", 1 }, "Eco Mode", false)
    };
}

void CarveAudioProcessor::prepareToPlay (double sampleRate, int)
{
    curOrder = (ecoParam->load() > 0.5f ? CarveEngine::maxOrder - 1 : CarveEngine::maxOrder);

    for (int i = 0; i < 2; ++i)
    {
        engines[i].setDepth (amountParam->load());
        engines[i].setSmoothness (smoothParam->load());
        engines[i].prepare (sampleRate, curOrder, i == 1);
    }

    mixSm.reset (sampleRate, 0.02);
    outSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTargetValue (mixParam->load());
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    holdA = holdB = 0;
    uiFftSize.store (1 << curOrder);
    setLatencySamples (1 << curOrder);
}

bool CarveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet()  != juce::AudioChannelSet::stereo()
     || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

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

void CarveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    // ---- Eco mode switch (quality/latency change, allowed to click briefly) ------
    const int wantOrder = (ecoParam->load() > 0.5f ? CarveEngine::maxOrder - 1
                                                   : CarveEngine::maxOrder);
    if (wantOrder != curOrder)
    {
        curOrder = wantOrder;
        engines[0].setOrder (curOrder);
        engines[1].setOrder (curOrder);
        pendingLatency.store (1 << curOrder);   // reported from the message thread
        uiFftSize.store (1 << curOrder);

        for (int k = (1 << curOrder) / 2 + 1; k < kNumBins; ++k)
        {
            displayRef[(size_t) k].store (0.0f);
            displayMain[(size_t) k].store (0.0f);
            displayCarve[(size_t) k].store (0.0f);
        }
    }

    // ---- parameters ---------------------------------------------------------------
    const float depthNow  = amountParam->load();
    const float smoothNow = smoothParam->load();
    engines[0].setDepth (depthNow);      engines[1].setDepth (depthNow);
    engines[0].setSmoothness (smoothNow); engines[1].setSmoothness (smoothNow);
    mixSm.setTargetValue (mixParam->load());
    outSm.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    // ---- sidechain activity gate (skips reference FFTs while silent) --------------
    auto refPeak = [this, &buffer, numSamples] (int busIndex) -> float
    {
        if (busIndex >= getBusCount (true))
            return -1.0f;
        auto* bus = getBus (true, busIndex);
        if (bus == nullptr || ! bus->isEnabled())
            return -1.0f;
        auto sc = getBusBuffer (buffer, true, busIndex);
        if (sc.getNumChannels() == 0)
            return -1.0f;

        float m = 0.0f;
        for (int ch = 0; ch < sc.getNumChannels(); ++ch)
            m = juce::jmax (m, sc.getMagnitude (ch, 0, numSamples));
        return m;
    };

    const float pA = refPeak (1);
    const float pB = refPeak (2);
    const int holdLen = (int) (getSampleRate() * 0.4);

    if (pA > 1.0e-4f) holdA = holdLen; else holdA = juce::jmax (0, holdA - numSamples);
    if (pB > 1.0e-4f) holdB = holdLen; else holdB = juce::jmax (0, holdB - numSamples);

    const bool aOn = pA >= 0.0f && holdA > 0;
    const bool bOn = pB >= 0.0f && holdB > 0;
    engines[0].setRefsActive (aOn, bOn);
    engines[1].setRefsActive (aOn, bOn);

    const bool connected = (pA >= 0.0f) || (pB >= 0.0f);
    uiState.store (! connected ? 0 : ((aOn || bOn) ? 2 : 1));

    // ---- audio --------------------------------------------------------------------
    auto mainIn = getBusBuffer (buffer, true, 0);
    if (mainIn.getNumChannels() == 0)
        return;

    const float* mL = mainIn.getReadPointer (0);
    const float* mR = mainIn.getNumChannels() > 1 ? mainIn.getReadPointer (1) : mL;

    auto refPtr = [this, &buffer] (int busIndex, const float*& l, const float*& r)
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
    refPtr (1, aL, aR);
    refPtr (2, bL, bR);

    auto mainOut = getBusBuffer (buffer, false, 0);
    float* oL = mainOut.getWritePointer (0);
    float* oR = mainOut.getNumChannels() > 1 ? mainOut.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float mixV = mixSm.getNextValue();
        const float outV = outSm.getNextValue();

        float dry, wet;
        engines[0].processSample (mL[i], aL != nullptr ? aL[i] : 0.0f,
                                         bL != nullptr ? bL[i] : 0.0f, dry, wet);
        const float left = (dry + mixV * (wet - dry)) * outV;

        engines[1].processSample (mR[i], aR != nullptr ? aR[i] : 0.0f,
                                         bR != nullptr ? bR[i] : 0.0f, dry, wet);
        const float right = (dry + mixV * (wet - dry)) * outV;

        oL[i] = left;
        if (oR != nullptr)
            oR[i] = right;
    }

    const float gLin = juce::jmin (engines[0].getCarvedGainLin(),
                                   engines[1].getCarvedGainLin());
    uiCarvedDb.store (juce::Decibels::gainToDecibels (gLin, -60.0f));
}

//==============================================================================
juce::AudioProcessorEditor* CarveAudioProcessor::createEditor()
{
    return new CarveAudioProcessorEditor (*this);
}

void CarveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CarveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CarveAudioProcessor();
}
