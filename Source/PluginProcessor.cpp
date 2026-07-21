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

    setupBands();
    reset();
}

void CarveEngine::setupBands()
{
    // Bark-like critical bands. The carve itself stays per-bin; these bands are only
    // used to DECIDE how deep to carve, because masking is a critical-band phenomenon.
    static const float edges[kNumBands + 1] =
    {
           20.0f,  100.0f,  200.0f,  300.0f,  400.0f,  510.0f,  630.0f,  770.0f,
          920.0f, 1080.0f, 1270.0f, 1480.0f, 1720.0f, 2000.0f, 2320.0f, 2700.0f,
         3150.0f, 3700.0f, 4400.0f, 5300.0f, 6400.0f, 7700.0f, 9500.0f, 12000.0f, 15500.0f
    };

    // Perceptual importance: peaks through the 1–4 kHz presence/intelligibility range,
    // stays high across the 200–800 Hz "mud" range, and deliberately backs off in the
    // sub so auto mode never guts the low end.
    static const float weights[kNumBands] =
    {
        0.15f, 0.40f, 0.90f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
        1.00f, 1.05f, 1.10f, 1.15f, 1.20f, 1.20f, 1.20f, 1.15f,
        1.10f, 1.00f, 0.85f, 0.70f, 0.55f, 0.40f, 0.30f, 0.25f
    };

    binHz = (float) (sr / (double) fftSize);
    lowBinMax = juce::jlimit (2, numBins, (int) (kLowHz / binHz));

    // Walk a running start bin so the bands stay contiguous and non-overlapping even
    // at high sample rates, where several Bark edges can collapse onto the same bin.
    // A band that would be empty is folded into the next one rather than double-counted.
    numBands = 0;
    int next = 1;
    for (int i = 0; i < kNumBands && next < numBins; ++i)
    {
        int b1 = (int) (edges[i + 1] / binHz);
        b1 = juce::jlimit (next, numBins, b1);

        if (b1 <= next)
            continue;                       // zero width here — merge into the next band

        bandEdge[(size_t) numBands]       = next;
        bandEdge[(size_t) (numBands + 1)] = b1;
        bandWeight[(size_t) numBands]     = weights[i];
        ++numBands;
        next = b1;
    }
}

void CarveEngine::measureRequiredDepth() noexcept
{
    // Sum energies into critical bands.
    float peakP = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        float sp = 0.0f, sm = 0.0f;
        for (int k = bandEdge[(size_t) b]; k < bandEdge[(size_t) (b + 1)]; ++k)
        {
            sp += refEnergySm[(size_t) k];
            sm += mainEBin[(size_t) k];
        }
        bandP[(size_t) b] = sp;
        bandM[(size_t) b] = sm;
        peakP = juce::jmax (peakP, sp);
    }

    if (peakP <= 1.0e-9f)
    {
        requiredDepth = 0.0f;
        return;
    }

    // Only bands where the priority source actually lives get a vote — we do not care
    // that the hats are loud at 10 kHz if the vocal has nothing up there.
    const float gate = peakP * 0.05f;

    // How far the priority sits below its target ratio, in dB, averaged over the bands
    // that matter. Deliberately NOT an algebraic inversion of the carve equation: that
    // form contains a 1/mask term which saturates the moment the priority drops even
    // 3 dB under the main content — i.e. on ordinary material — pinning auto mode at
    // maximum depth. Masking excess is bounded, monotonic and behaves like the ear.
    float wsum = 0.0f, dsum = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        const float P = bandP[(size_t) b];
        if (P < gate)
            continue;

        const float M = bandM[(size_t) b] + 1.0e-12f;
        const float R = P / M;                       // priority-to-masker energy ratio

        // Bands that are already clear stay in the average contributing zero, so a mix
        // that only collides here and there is not treated like one that collides
        // everywhere.
        const float excessDb = juce::jlimit (0.0f, kMaxExcessDb,
                                             10.0f * std::log10 (kTargetR / R));

        const float w = bandWeight[(size_t) b] * P;
        dsum += w * excessDb;
        wsum += w;
    }

    // kMaxExcessDb of average masking excess earns full depth; parity earns a gentle
    // touch. Monotonic and bounded by construction — it cannot run away.
    requiredDepth = wsum > 0.0f
                      ? juce::jlimit (0.0f, 1.0f, (dsum / wsum) / kMaxExcessDb)
                      : 0.0f;
}

void CarveEngine::reset() noexcept
{
    pos = 0;
    hopCount = stagger ? hopSize / 2 : 0;

    mainFifo.fill (0.0f);  refFifoA.fill (0.0f);  refFifoB.fill (0.0f);
    mainDelay.fill (0.0f); ola.fill (0.0f);
    refEnergySm.fill (0.0f);
    mainEBin.fill (0.0f);
    gain.fill (1.0f);

    depth = targetDepth;
    carvedGainLin = 1.0f;
    lowCarveGain = 1.0f;
    requiredDepth = 0.0f;
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
        mainEBin[(size_t) k] = mE;
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

    // Measure what depth the current collision would require. Done on the INPUT
    // spectra (open loop), so auto mode cannot chase its own output.
    if (anyRef)
        measureRequiredDepth();
    else
        requiredDepth = 0.0f;

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

    // Mean carve below kLowHz: what the user's laptop speakers physically cannot show them.
    {
        float lowSum = 0.0f;
        int lowCount = 0;
        for (int k = 1; k < lowBinMax; ++k)
        {
            lowSum += gSmooth[(size_t) k];
            ++lowCount;
        }
        lowCarveGain = lowCount > 0 ? lowSum / (float) lowCount : 1.0f;
    }

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
    autoParam   = apvts.getRawParameterValue ("autocal");

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
            juce::ParameterID { "eco", 1 }, "Eco Mode", false),
        std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "autocal", 1 }, "Auto Calibrate", true)
    };
}

void CarveAudioProcessor::prepareToPlay (double sampleRate, int)
{
    curOrder = (ecoParam->load() > 0.5f ? CarveEngine::maxOrder - 1 : CarveEngine::maxOrder);

    // In auto mode the depth is derived every block, so start the engines from silence
    // rather than seeding them with the Amount knob (which now means bias, not depth).
    const float seedDepth = autoParam->load() > 0.5f ? 0.0f : amountParam->load();

    for (int i = 0; i < 2; ++i)
    {
        engines[i].setDepth (seedDepth);
        engines[i].setSmoothness (smoothParam->load());
        engines[i].prepare (sampleRate, curOrder, i == 1);
    }

    mixSm.reset (sampleRate, 0.02);
    outSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTargetValue (mixParam->load());
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    holdA = holdB = 0;
    autoDepthSm = 0.0f;
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

    // ---- sidechain activity gate (skips reference FFTs while silent) --------------
    // Runs before the parameter stage because auto-calibration needs to know whether a
    // priority source is actually present this block.
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

    // ---- parameters ---------------------------------------------------------------
    const float amountNow = amountParam->load();
    const float smoothNow = smoothParam->load();
    const bool  autoOn    = autoParam->load() > 0.5f;

    float depthNow;

    if (autoOn)
    {
        // Freeze the calibration while the priority source is silent. Vocals pause
        // between phrases; without this the settled value would bleed away during every
        // gap and have to climb back, making the carve breathe across the performance.
        // Nothing is carved meanwhile anyway — an absent reference gives a zero mask.
        if (aOn || bOn)
        {
            // Both engines measure the same programme material; average them so the two
            // channels never drift into a stereo imbalance.
            const float measured = 0.5f * (engines[0].getRequiredDepth()
                                         + engines[1].getRequiredDepth());

            // Settle over seconds, not milliseconds: we are calibrating to the character
            // of the song, not reacting to individual transients. Rises a little faster
            // than it falls so a busy chorus is covered promptly.
            const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
            const double tc = measured > autoDepthSm ? 1.5 : 3.0;
            const float  c  = juce::jlimit (0.0f, 1.0f,
                                  1.0f - (float) std::exp (-(double) numSamples / (sr * tc)));
            autoDepthSm += c * (measured - autoDepthSm);
        }

        // The Amount knob becomes a bias: 0.5 = trust the measurement, and each half
        // of the range doubles or halves it.
        const float bias = std::pow (2.0f, (amountNow - 0.5f) * 2.0f);
        depthNow = juce::jlimit (0.0f, 0.95f, autoDepthSm * bias);
    }
    else
    {
        depthNow = amountNow;
    }

    uiAppliedDepth.store (depthNow);
    engines[0].setDepth (depthNow);      engines[1].setDepth (depthNow);
    engines[0].setSmoothness (smoothNow); engines[1].setSmoothness (smoothNow);
    mixSm.setTargetValue (mixParam->load());
    outSm.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

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

    // Report what actually reaches the output: at Mix < 100 % the dry path fills part of
    // the hole back in, so the raw carve figure would overstate the attenuation.
    const float mixNow = mixParam->load();
    auto effective = [mixNow] (float gLin)
    {
        return juce::Decibels::gainToDecibels (1.0f + mixNow * (gLin - 1.0f), -60.0f);
    };

    uiCarvedDb.store (effective (juce::jmin (engines[0].getCarvedGainLin(),
                                             engines[1].getCarvedGainLin())));
    uiLowCarvedDb.store (effective (juce::jmin (engines[0].getLowCarveGainLin(),
                                                engines[1].getLowCarveGainLin())));
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
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    // Sessions saved before auto-calibration existed stored Amount as a raw depth. If we
    // let the parameter fall back to its "on" default, those mixes would reopen with the
    // knob silently reinterpreted as a bias and play back differently. Absent means old.
    bool hasAutoCal = false;
    for (int i = 0; i < tree.getNumChildren(); ++i)
        if (tree.getChild (i).getProperty ("id").toString() == "autocal")
            hasAutoCal = true;

    apvts.replaceState (tree);

    if (! hasAutoCal)
        if (auto* p = apvts.getParameter ("autocal"))
            p->setValueNotifyingHost (0.0f);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CarveAudioProcessor();
}
