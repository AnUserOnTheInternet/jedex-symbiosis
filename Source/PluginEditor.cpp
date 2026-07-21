#include "PluginEditor.h"

using namespace symbiosis;

//==============================================================================
// SymbiosisLookAndFeel — machined-metal rotary with lit tick ring
//==============================================================================
void SymbiosisLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                             int width, int height,
                                             float sliderPos, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                                (float) width, (float) height).reduced (10.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // ---- tick ring (outside the value arc) -----------------------------------------
    constexpr int numTicks = 25;
    for (int i = 0; i < numTicks; ++i)
    {
        const float t = (float) i / (float) (numTicks - 1);
        const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        const bool lit = a <= angle + 0.001f;

        g.setColour (lit ? colours::neonBlue.interpolatedWith (colours::neonPurple, t)
                               .withAlpha (0.85f)
                         : juce::Colour (0xff2a2c33));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (radius + 3.0f, a),
                                       centre.getPointOnCircumference (radius + 7.0f, a)),
                    lit ? 1.8f : 1.2f);
    }

    // ---- value arc -----------------------------------------------------------------
    const float arcR = radius - 4.0f;
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff191b20));
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             rotaryStartAngle, angle, true);

        g.setColour (colours::neonBlue.withAlpha (0.16f));   // glow pass
        g.strokePath (value, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::ColourGradient grad (colours::neonBlue,
                                   centre.x - radius, centre.y + radius,
                                   colours::neonPurple,
                                   centre.x + radius, centre.y - radius, false);
        g.setGradientFill (grad);
        g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // ---- body: drop shadow, bezel, knurl, face -------------------------------------
    const float bodyR = radius * 0.76f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);

    {
        juce::Path shadowPath;
        shadowPath.addEllipse (body);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 22, { 0, 8 })
            .drawForPath (g, shadowPath);
    }

    {
        juce::ColourGradient bezel (juce::Colour (0xff484b53), centre.x, body.getY(),
                                    juce::Colour (0xff0b0c0f), centre.x, body.getBottom(), false);
        g.setGradientFill (bezel);
        g.fillEllipse (body);

        // thin rim light on the upper edge
        juce::Path rim;
        rim.addCentredArc (centre.x, centre.y, bodyR - 0.8f, bodyR - 0.8f, 0.0f,
                           -2.4f, -0.6f, true);
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.strokePath (rim, juce::PathStrokeType (1.2f));
    }

    // knurled grip on the bezel ring
    for (int i = 0; i < 48; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 48.0f;
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (bodyR * 0.92f, a),
                                       centre.getPointOnCircumference (bodyR * 0.99f, a)), 1.4f);
    }

    const auto face = body.reduced (bodyR * 0.11f);
    const float faceR = face.getWidth() * 0.5f;
    {
        juce::ColourGradient faceGrad (juce::Colour (0xff26282f),
                                       centre.x - bodyR * 0.4f, centre.y - bodyR * 0.5f,
                                       juce::Colour (0xff0d0e11),
                                       centre.x + bodyR * 0.5f, centre.y + bodyR * 0.7f, true);
        g.setGradientFill (faceGrad);
        g.fillEllipse (face);
    }

    // brushed-metal hairlines (deterministic pseudo-random alpha)
    for (int i = 0; i < 84; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 84.0f;
        const float alpha = 0.012f + 0.020f * (0.5f + 0.5f * std::sin ((float) i * 12.9898f));
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (faceR * 0.34f, a),
                                       centre.getPointOnCircumference (faceR * 0.97f, a)), 1.0f);
    }

    // diagonal sheen + glass highlight
    {
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.06f),
                                    face.getX(), face.getY(),
                                    juce::Colours::transparentWhite,
                                    centre.x, centre.y, false);
        g.setGradientFill (sheen);
        g.fillEllipse (face);

        auto glass = face.withHeight (face.getHeight() * 0.46f).reduced (faceR * 0.20f, 0.0f);
        juce::ColourGradient glassGrad (juce::Colours::white.withAlpha (0.08f),
                                        glass.getCentreX(), glass.getY(),
                                        juce::Colours::transparentWhite,
                                        glass.getCentreX(), glass.getBottom(), false);
        g.setGradientFill (glassGrad);
        g.fillEllipse (glass);
    }

    // ---- pointer -------------------------------------------------------------------
    {
        const auto inner = centre.getPointOnCircumference (faceR * 0.58f, angle);
        const auto outer = centre.getPointOnCircumference (faceR * 0.90f, angle);
        g.setColour (colours::neonBlue.withAlpha (0.22f));
        g.drawLine (juce::Line<float> (inner, outer), 6.0f);
        g.setColour (colours::neonBlue);
        g.drawLine (juce::Line<float> (inner, outer), 2.2f);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (outer));
    }

    // ---- centre read-out -----------------------------------------------------------
    g.setColour (juce::Colours::white.withAlpha (0.88f));
    g.setFont (juce::Font (juce::FontOptions (24.0f)).withExtraKerningFactor (0.04f));
    g.drawText (juce::String (juce::roundToInt (slider.getValue() * 100.0)) + "%",
                body.translated (0.0f, -8.0f).toNearestInt(), juce::Justification::centred);

    g.setColour (juce::Colours::white.withAlpha (0.30f));
    g.setFont (juce::Font (juce::FontOptions (9.5f)).withExtraKerningFactor (0.32f));
    g.drawText ("SYMBIOSIS", body.translated (0.0f, 17.0f).toNearestInt(),
                juce::Justification::centred);
}

//==============================================================================
// RadarVisualizer
//==============================================================================
RadarVisualizer::RadarVisualizer (JeDExSymbiosisAudioProcessor& p)
    : processor (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

RadarVisualizer::~RadarVisualizer()
{
    stopTimer();
}

void RadarVisualizer::timerCallback()
{
    const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const float binHz = (float) (sr / (double) SpectralEngine::fftSize);

    constexpr float fLo = 30.0f, fHi = 16000.0f;
    const float magRef = (float) SpectralEngine::fftSize * 0.25f;   // full-scale sine peak

    auto toNorm = [magRef] (float mag)
    {
        const float db = juce::Decibels::gainToDecibels (mag / magRef, -80.0f);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    for (int i = 0; i < kPoints; ++i)
    {
        const float t0 = (float) i       / (float) kPoints;
        const float t1 = (float) (i + 1) / (float) kPoints;

        int b0 = (int) (fLo * std::pow (fHi / fLo, t0) / binHz);
        int b1 = (int) (fLo * std::pow (fHi / fLo, t1) / binHz);
        b0 = juce::jlimit (1, SpectralEngine::numBins - 1, b0);
        b1 = juce::jlimit (b0 + 1, SpectralEngine::numBins, b1);

        float m = 0.0f, s = 0.0f, c = 0.0f;
        for (int b = b0; b < b1; ++b)
        {
            m = juce::jmax (m, processor.displayMain[(size_t) b].load (std::memory_order_relaxed));
            s = juce::jmax (s, processor.displaySide[(size_t) b].load (std::memory_order_relaxed));
            c = juce::jmax (c, processor.displayCarve[(size_t) b].load (std::memory_order_relaxed));
        }

        // Temporal easing keeps the radar fluid at 30 fps.
        mainCurve[(size_t) i]  += 0.45f * (toNorm (m) - mainCurve[(size_t) i]);
        sideCurve[(size_t) i]  += 0.45f * (toNorm (s) - sideCurve[(size_t) i]);
        carveCurve[(size_t) i] += 0.50f * (c          - carveCurve[(size_t) i]);
    }

    sweepPhase = std::fmod (sweepPhase + 0.035f, juce::MathConstants<float>::twoPi);
    repaint();
}

void RadarVisualizer::paint (juce::Graphics& g)
{
    const auto centre = getLocalBounds().getCentre().toFloat();
    const float outerR = (float) juce::jmin (getWidth(), getHeight()) * 0.5f - 50.0f;
    const float innerR = kInnerRadius;

    if (outerR <= innerR)
        return;

    constexpr float fLo = 30.0f, fHi = 16000.0f;

    // ---- HUD chrome: tick ring, outer circle, frequency markers --------------------
    for (int i = 0; i < 72; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 72.0f;
        const bool major = (i % 6 == 0);
        g.setColour (juce::Colours::white.withAlpha (major ? 0.10f : 0.045f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (outerR + 6.0f, a),
                                       centre.getPointOnCircumference (outerR + (major ? 14.0f : 10.0f), a)),
                    1.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (juce::Rectangle<float> ((outerR + 2.0f) * 2.0f, (outerR + 2.0f) * 2.0f)
                       .withCentre (centre), 1.0f);

    {
        g.setFont (juce::Font (juce::FontOptions (9.0f)).withExtraKerningFactor (0.10f));
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        const std::pair<float, const char*> marks[] = { { 100.0f,   "100" },
                                                        { 1000.0f,  "1K"  },
                                                        { 10000.0f, "10K" } };
        for (const auto& mark : marks)
        {
            const float a = juce::MathConstants<float>::twoPi
                          * std::log (mark.first / fLo) / std::log (fHi / fLo);
            const auto pos = centre.getPointOnCircumference (outerR + 24.0f, a);
            g.drawText (mark.second,
                        juce::Rectangle<float> (30.0f, 12.0f).withCentre (pos).toNearestInt(),
                        juce::Justification::centred);
        }
    }

    // Faint concentric grid.
    g.setColour (juce::Colours::white.withAlpha (0.040f));
    for (float f : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const float r = innerR + f * (outerR - innerR);
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), 1.0f);
    }

    // ---- rotating sweep with fading trail ------------------------------------------
    for (int j = 13; j >= 0; --j)
    {
        const float a = sweepPhase - 0.030f * (float) j;
        const float alpha = 0.11f * (1.0f - (float) j / 14.0f);
        g.setColour (colours::neonBlue.withAlpha (alpha));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (innerR, a),
                                       centre.getPointOnCircumference (outerR, a)),
                    j == 0 ? 2.0f : 1.5f);
    }

    // ---- helpers -------------------------------------------------------------------
    auto radiusFor = [innerR, outerR] (float v)
    {
        return innerR + juce::jlimit (0.0f, 1.0f, v) * (outerR - innerR);
    };

    auto pointAt = [&centre] (int i, float r)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) kPoints;
        return centre.getPointOnCircumference (r, a);
    };

    // Smooth closed ring through the data points (quadratic midpoint interpolation).
    auto buildSmoothRing = [&] (const std::array<float, kPoints>& data)
    {
        std::array<juce::Point<float>, kPoints> pts;
        for (int i = 0; i < kPoints; ++i)
            pts[(size_t) i] = pointAt (i, radiusFor (data[(size_t) i]));

        juce::Path p;
        p.startNewSubPath ((pts[(size_t) (kPoints - 1)] + pts[0]) * 0.5f);
        for (int i = 0; i < kPoints; ++i)
        {
            const auto& cur = pts[(size_t) i];
            const auto& nxt = pts[(size_t) ((i + 1) % kPoints)];
            p.quadraticTo (cur, (cur + nxt) * 0.5f);
        }
        p.closeSubPath();
        return p;
    };

    // ---- carved spectrum: radial magenta rays under everything ---------------------
    for (int i = 0; i < kPoints; ++i)
    {
        const float c = carveCurve[(size_t) i];
        if (c > 0.10f)
        {
            const auto from = pointAt (i, innerR + 2.0f);
            const auto to   = pointAt (i, radiusFor (sideCurve[(size_t) i]));
            g.setColour (colours::carveMagenta.withAlpha (juce::jmin (0.30f, c * 0.30f)));
            g.drawLine (juce::Line<float> (from, to), 4.0f);
            g.setColour (colours::carveMagenta.withAlpha (juce::jmin (0.90f, c)));
            g.drawLine (juce::Line<float> (from, to), 1.8f);
        }
    }

    // ---- sidechain layers (pre-carve), purple --------------------------------------
    {
        const auto p = buildSmoothRing (sideCurve);
        g.setColour (colours::neonPurple.withAlpha (0.10f));
        g.strokePath (p, juce::PathStrokeType (5.0f));
        g.setColour (colours::neonPurple.withAlpha (0.60f));
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    // ---- main layer, neon blue: gradient area fill + layered glow ------------------
    {
        const auto p = buildSmoothRing (mainCurve);

        juce::ColourGradient fill (colours::neonBlue.withAlpha (0.0f), centre.x, centre.y,
                                   colours::neonBlue.withAlpha (0.14f),
                                   centre.x + outerR, centre.y, true);
        g.setGradientFill (fill);
        g.fillPath (p);

        g.setColour (colours::neonBlue.withAlpha (0.10f));
        g.strokePath (p, juce::PathStrokeType (7.0f));
        g.setColour (colours::neonBlue.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (3.5f));
        g.setColour (colours::neonBlue);
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }

    // Soft halo where the ring meets the knob.
    g.setColour (colours::neonBlue.withAlpha (0.05f));
    g.drawEllipse (juce::Rectangle<float> ((innerR - 5.0f) * 2.0f, (innerR - 5.0f) * 2.0f)
                       .withCentre (centre), 8.0f);
}

//==============================================================================
// JeDExSymbiosisAudioProcessorEditor
//==============================================================================
JeDExSymbiosisAudioProcessorEditor::JeDExSymbiosisAudioProcessorEditor (JeDExSymbiosisAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), radar (p)
{
    setOpaque (true);

    addAndMakeVisible (radar);

    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setDoubleClickReturnValue (true, 0.5);
    knob.setLookAndFeel (&lookAndFeel);
    knob.setBufferedToImage (true);   // radar repaints at 30 fps underneath —
                                      // cache the knob so its shadow blur isn't re-rendered

    addAndMakeVisible (knob);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "symbiosis", knob);

    setSize (kEditorWidth, kEditorHeight);
}

JeDExSymbiosisAudioProcessorEditor::~JeDExSymbiosisAudioProcessorEditor()
{
    knob.setLookAndFeel (nullptr);
}

void JeDExSymbiosisAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto centre = b.getCentre();

    // Matte base.
    g.fillAll (colours::background);

    // Aurora washes — barely-there blue top-left, purple bottom-right.
    {
        juce::ColourGradient wash1 (colours::neonBlue.withAlpha (0.040f),
                                    b.getWidth() * 0.22f, b.getHeight() * 0.16f,
                                    colours::neonBlue.withAlpha (0.0f),
                                    b.getWidth() * 0.85f, b.getHeight() * 0.75f, true);
        g.setGradientFill (wash1);
        g.fillAll();

        juce::ColourGradient wash2 (colours::neonPurple.withAlpha (0.035f),
                                    b.getWidth() * 0.80f, b.getHeight() * 0.86f,
                                    colours::neonPurple.withAlpha (0.0f),
                                    b.getWidth() * 0.15f, b.getHeight() * 0.20f, true);
        g.setGradientFill (wash2);
        g.fillAll();
    }

    // Vignette.
    {
        juce::ColourGradient vignette (juce::Colours::transparentBlack,
                                       centre.x, centre.y,
                                       juce::Colours::black.withAlpha (0.42f),
                                       centre.x, 0.0f, true);
        g.setGradientFill (vignette);
        g.fillAll();
    }

    // ---- header --------------------------------------------------------------------
    {
        auto title = getLocalBounds().removeFromTop (34);
        auto left  = title.removeFromLeft (getWidth() / 2 - 6);

        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (juce::Font (juce::FontOptions (20.0f)).boldened());
        g.drawText ("JeDEx", left, juce::Justification::centredRight);

        g.setColour (colours::neonBlue.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (18.0f)).withExtraKerningFactor (0.30f));
        g.drawText (" SYMBIOSIS", title, juce::Justification::centredLeft);

        g.setColour (juce::Colours::white.withAlpha (0.26f));
        g.setFont (juce::Font (juce::FontOptions (8.5f)).withExtraKerningFactor (0.38f));
        g.drawText ("DYNAMIC HARMONIC CARVING",
                    juce::Rectangle<int> (0, 34, getWidth(), 12), juce::Justification::centred);

        // decorative rules flanking the title
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        const float ry = 20.0f;
        g.drawLine (centre.x - 220.0f, ry, centre.x - 140.0f, ry, 1.0f);
        g.drawLine (centre.x + 140.0f, ry, centre.x + 220.0f, ry, 1.0f);
    }

    // ---- footer legend -------------------------------------------------------------
    {
        struct Entry { juce::Colour colour; const char* text; int width; };
        const Entry entries[] = { { colours::neonBlue,     "MAIN",       40 },
                                  { colours::neonPurple,   "LAYERS B+C", 88 },
                                  { colours::carveMagenta, "CARVED",     58 } };

        g.setFont (juce::Font (juce::FontOptions (9.5f)).withExtraKerningFactor (0.22f));

        int total = 0;
        for (const auto& e : entries) total += 14 + e.width + 22;
        total -= 22;

        int xPos = (getWidth() - total) / 2;
        const int yPos = getHeight() - 30;

        for (const auto& e : entries)
        {
            g.setColour (e.colour.withAlpha (0.85f));
            g.fillEllipse ((float) xPos, (float) yPos - 3.0f, 6.0f, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.38f));
            g.drawText (e.text, xPos + 14, yPos - 8, e.width, 12, juce::Justification::centredLeft);
            xPos += 14 + e.width + 22;
        }
    }
}

void JeDExSymbiosisAudioProcessorEditor::resized()
{
    radar.setBounds (getLocalBounds());
    knob.setBounds (juce::Rectangle<int> (kKnobSize, kKnobSize)
                        .withCentre (getLocalBounds().getCentre()));
}
