#include "PluginEditor.h"
#include "BinaryData.h"

using namespace carve;

//==============================================================================
// CarveLookAndFeel
//==============================================================================
void CarveLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                         int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    const auto all = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto labelStrip = all.withTop (all.getBottom() - 16.0f);
    const auto knobArea   = all.withTrimmedBottom (18.0f);

    const auto centre = knobArea.getCentre();
    const float radius = juce::jmin (knobArea.getWidth(), knobArea.getHeight()) * 0.5f - 8.0f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const bool  big    = radius > 52.0f;

    const juce::String valueText = slider.getTextFromValue (slider.getValue());

    if (big)
    {
        // ---- tick ring ------------------------------------------------------------
        constexpr int numTicks = 21;
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = (float) i / (float) (numTicks - 1);
            const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const bool lit = a <= angle + 0.001f;
            g.setColour (lit ? colours::ref.interpolatedWith (colours::bus, t).withAlpha (0.85f)
                             : juce::Colour (0xff26282f));
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (radius + 3.0f, a),
                                           centre.getPointOnCircumference (radius + 7.0f, a)),
                        lit ? 1.8f : 1.2f);
        }

        // ---- value arc ------------------------------------------------------------
        const float arcR = radius - 3.0f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff191b20));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             rotaryStartAngle, angle, true);
        g.setColour (colours::ref.withAlpha (0.15f));
        g.strokePath (value, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        juce::ColourGradient grad (colours::ref, centre.x - radius, centre.y + radius,
                                   colours::bus, centre.x + radius, centre.y - radius, false);
        g.setGradientFill (grad);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // ---- metallic body --------------------------------------------------------
        const float bodyR = radius * 0.74f;
        const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);

        {
            juce::Path shadowPath;
            shadowPath.addEllipse (body);
            juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 18, { 0, 6 })
                .drawForPath (g, shadowPath);
        }
        {
            juce::ColourGradient bezel (juce::Colour (0xff484b53), centre.x, body.getY(),
                                        juce::Colour (0xff0b0c0f), centre.x, body.getBottom(), false);
            g.setGradientFill (bezel);
            g.fillEllipse (body);
        }
        for (int i = 0; i < 40; ++i)   // knurled grip
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 40.0f;
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (bodyR * 0.92f, a),
                                           centre.getPointOnCircumference (bodyR * 0.99f, a)), 1.3f);
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
        for (int i = 0; i < 60; ++i)   // brushed hairlines
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 60.0f;
            const float alpha = 0.012f + 0.02f * (0.5f + 0.5f * std::sin ((float) i * 12.9898f));
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (faceR * 0.34f, a),
                                           centre.getPointOnCircumference (faceR * 0.96f, a)), 1.0f);
        }

        // pointer
        {
            const auto inner = centre.getPointOnCircumference (faceR * 0.58f, angle);
            const auto outer = centre.getPointOnCircumference (faceR * 0.90f, angle);
            g.setColour (colours::ref.withAlpha (0.22f));
            g.drawLine (juce::Line<float> (inner, outer), 5.0f);
            g.setColour (colours::ref);
            g.drawLine (juce::Line<float> (inner, outer), 2.0f);
        }

        // centre read-out — in auto mode this knob trims the measurement, so show the
        // offset from neutral rather than a raw depth percentage.
        if (autoMode)
        {
            // Report the multiplier actually applied to the measurement (2^((v-0.5)*2)),
            // not the knob's raw travel — otherwise the bottom of the range would claim
            // "-100%" while really only halving the depth.
            const float bias = std::pow (2.0f, ((float) slider.getValue() - 0.5f) * 2.0f);
            const int   pct  = juce::roundToInt ((bias - 1.0f) * 100.0f);

            g.setColour (juce::Colours::white.withAlpha (0.88f));
            g.setFont (juce::Font (juce::FontOptions (19.0f)).withExtraKerningFactor (0.04f));
            // No status claim here: the knob cannot know whether a measurement is
            // actually running, and a green "CALIBRATED" badge shown with no sidechain
            // connected would simply be untrue. The caption under the knob already says
            // AUTO TRIM, and the pill above reports the real state.
            g.drawText (pct == 0 ? "AUTO" : (pct > 0 ? "+" + juce::String (pct) + "%"
                                                     : juce::String (pct) + "%"),
                        body.toNearestInt(), juce::Justification::centred);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.88f));
            g.setFont (juce::Font (juce::FontOptions (21.0f)).withExtraKerningFactor (0.04f));
            g.drawText (valueText, body.toNearestInt(), juce::Justification::centred);
        }
    }
    else
    {
        // ---- flat small knob ------------------------------------------------------
        const auto face = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
        g.setColour (juce::Colour (0xff15161b));
        g.fillEllipse (face);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawEllipse (face, 1.0f);

        const float arcR = radius + 4.0f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff191b20));
        g.strokePath (track, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             rotaryStartAngle, angle, true);
        juce::ColourGradient grad (colours::ref, centre.x - arcR, centre.y + arcR,
                                   colours::bus, centre.x + arcR, centre.y - arcR, false);
        g.setGradientFill (grad);
        g.strokePath (value, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        const auto tip = centre.getPointOnCircumference (radius * 0.78f, angle);
        g.setColour (colours::ref);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (tip));

        g.setColour (juce::Colours::white.withAlpha (0.80f));
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText (valueText, face.toNearestInt(), juce::Justification::centred);
    }

    g.setColour (juce::Colours::white.withAlpha (0.42f));
    g.setFont (juce::Font (juce::FontOptions (9.5f)).withExtraKerningFactor (0.28f));
    g.drawText (slider.getName(), labelStrip.toNearestInt(), juce::Justification::centred);
}

void CarveLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool highlighted, bool)
{
    const auto b = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    const auto pill = juce::Rectangle<float> (44.0f, 22.0f)
                          .withCentre ({ b.getX() + 24.0f, b.getCentreY() });

    g.setColour (on ? colours::active.withAlpha (0.22f) : juce::Colour (0xff17181c));
    g.fillRoundedRectangle (pill, 11.0f);
    g.setColour (on ? colours::active.withAlpha (0.85f)
                    : juce::Colours::white.withAlpha (highlighted ? 0.25f : 0.14f));
    g.drawRoundedRectangle (pill, 11.0f, 1.2f);

    const float thumbX = on ? pill.getRight() - 12.0f : pill.getX() + 12.0f;
    if (on)
    {
        g.setColour (colours::active.withAlpha (0.30f));
        g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ thumbX, pill.getCentreY() }));
    }
    g.setColour (on ? colours::active : juce::Colour (0xff5a5d66));
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ thumbX, pill.getCentreY() }));

    g.setColour (juce::Colours::white.withAlpha (on ? 0.85f : 0.45f));
    g.setFont (juce::Font (juce::FontOptions (10.5f)).withExtraKerningFactor (0.16f));
    g.drawText (button.getButtonText(),
                b.withTrimmedLeft (56.0f).toNearestInt(), juce::Justification::centredLeft);
}

void CarveLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                             const juce::Colour&, bool highlighted, bool down)
{
    const auto b = button.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.10f : (highlighted ? 0.07f : 0.03f)));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (b, 6.0f, 1.0f);
}

void CarveLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (10.5f)).withExtraKerningFactor (0.22f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
// SpectrumView
//==============================================================================
namespace
{
    constexpr float kFLo = 30.0f, kFHi = 16000.0f;

    float freqToNorm (float f)
    {
        return std::log (f / kFLo) / std::log (kFHi / kFLo);
    }
}

SpectrumView::SpectrumView (CarveAudioProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

SpectrumView::~SpectrumView()
{
    stopTimer();
}

void SpectrumView::resized()
{
    bgCache = juce::Image();
}

void SpectrumView::rebuildBackground()
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    // Render the static chrome at 2x and blit it scaled, so the grid, labels and legend
    // stay crisp on 125–200 % display scaling instead of being upsampled from a
    // logical-size bitmap while everything drawn live around them is native resolution.
    bgScale = 2;
    bgCache = juce::Image (juce::Image::ARGB, w * bgScale, h * bgScale, true);
    juce::Graphics g (bgCache);
    g.addTransform (juce::AffineTransform::scale ((float) bgScale));

    const auto frame = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (frame, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (frame.reduced (0.5f), 10.0f, 1.0f);

    const auto plot = frame.reduced (14.0f, 12.0f);

    // frequency grid + labels
    const struct { float f; const char* t; } marks[] = {
        { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
        { 1000, "1K" }, { 2000, "2K" }, { 5000, "5K" }, { 10000, "10K" } };

    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    for (const auto& m : marks)
    {
        const float xx = plot.getX() + freqToNorm (m.f) * plot.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawLine (xx, plot.getY() + 6.0f, xx, plot.getBottom() - 14.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.drawText (m.t, (int) xx - 14, (int) plot.getBottom() - 12, 28, 10,
                    juce::Justification::centred);
    }

    // centre line
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawLine (plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), 1.0f);

    // legend
    const struct { juce::Colour c; const char* t; int w; } legend[] = {
        { colours::ref,    "PRIORITY", 62 },
        { colours::bus,    "YOUR MIX", 62 },
        { colours::carved, "CARVED",   50 } };

    int lx = (int) plot.getX() + 4;
    const int ly = (int) plot.getY() + 4;
    g.setFont (juce::Font (juce::FontOptions (9.0f)).withExtraKerningFactor (0.18f));
    for (const auto& e : legend)
    {
        g.setColour (e.c.withAlpha (0.9f));
        g.fillEllipse ((float) lx, (float) ly + 3.0f, 6.0f, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.42f));
        g.drawText (e.t, lx + 10, ly, e.w, 12, juce::Justification::centredLeft);
        lx += 10 + e.w + 10;
    }
}

void SpectrumView::timerCallback()
{
    const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const int fftSize = processor.uiFftSize.load();
    const int numBins = fftSize / 2 + 1;
    const float binHz = (float) (sr / (double) fftSize);
    const float magRef = (float) fftSize * 0.25f;

    auto toNorm = [magRef] (float mag)
    {
        const float db = juce::Decibels::gainToDecibels (mag / magRef, -80.0f);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    for (int i = 0; i < kPts; ++i)
    {
        const float t0 = (float) i       / (float) kPts;
        const float t1 = (float) (i + 1) / (float) kPts;

        int b0 = (int) (kFLo * std::pow (kFHi / kFLo, t0) / binHz);
        int b1 = (int) (kFLo * std::pow (kFHi / kFLo, t1) / binHz);
        b0 = juce::jlimit (1, numBins - 1, b0);
        b1 = juce::jlimit (b0 + 1, numBins, b1);

        float r = 0.0f, m = 0.0f, c = 0.0f;
        for (int b = b0; b < b1; ++b)
        {
            r = juce::jmax (r, processor.displayRef[(size_t) b].load (std::memory_order_relaxed));
            m = juce::jmax (m, processor.displayMain[(size_t) b].load (std::memory_order_relaxed));
            c = juce::jmax (c, processor.displayCarve[(size_t) b].load (std::memory_order_relaxed));
        }

        refCurve[(size_t) i]   += 0.5f * (toNorm (r) - refCurve[(size_t) i]);
        mainCurve[(size_t) i]  += 0.5f * (toNorm (m) - mainCurve[(size_t) i]);
        carveCurve[(size_t) i] += 0.5f * (c          - carveCurve[(size_t) i]);
    }

    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    if (! bgCache.isValid())
        rebuildBackground();
    g.drawImage (bgCache, getLocalBounds().toFloat());

    const auto plot = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
    const float cy = plot.getCentreY();
    const float H2 = plot.getHeight() * 0.5f - 22.0f;
    const bool eco = processor.apvts.getRawParameterValue ("eco")->load() > 0.5f;

    auto xAt = [&plot] (int i)
    {
        return plot.getX() + plot.getWidth() * (float) i / (float) (kPts - 1);
    };

    // Smoothed mirrored area around the centre line.
    auto areaPath = [&] (const std::array<float, kPts>& v)
    {
        juce::Path p;
        p.startNewSubPath (xAt (0), cy - v[0] * H2);
        for (int i = 1; i < kPts; ++i)
            p.quadraticTo (xAt (i - 1), cy - v[(size_t) (i - 1)] * H2,
                           0.5f * (xAt (i - 1) + xAt (i)),
                           cy - 0.5f * (v[(size_t) (i - 1)] + v[(size_t) i]) * H2);
        p.lineTo (xAt (kPts - 1), cy - v[(size_t) (kPts - 1)] * H2);
        p.lineTo (xAt (kPts - 1), cy + v[(size_t) (kPts - 1)] * H2);
        for (int i = kPts - 1; i > 0; --i)
            p.quadraticTo (xAt (i), cy + v[(size_t) i] * H2,
                           0.5f * (xAt (i - 1) + xAt (i)),
                           cy + 0.5f * (v[(size_t) (i - 1)] + v[(size_t) i]) * H2);
        p.closeSubPath();
        return p;
    };

    // Band between an outer and an inner curve, on one side of the centre line.
    auto ribbonPath = [&] (const std::array<float, kPts>& outer,
                           const std::array<float, kPts>& inner, float sign)
    {
        juce::Path p;
        p.startNewSubPath (xAt (0), cy + sign * outer[0] * H2);
        for (int i = 1; i < kPts; ++i)
            p.lineTo (xAt (i), cy + sign * outer[(size_t) i] * H2);
        for (int i = kPts - 1; i >= 0; --i)
            p.lineTo (xAt (i), cy + sign * inner[(size_t) i] * H2);
        p.closeSubPath();
        return p;
    };

    // ---- priority sources (blue) --------------------------------------------------
    {
        const auto p = areaPath (refCurve);
        juce::ColourGradient fill (colours::ref.withAlpha (0.26f), plot.getX(), cy,
                                   colours::ref.withAlpha (0.04f), plot.getX(), plot.getY(), false);
        g.setGradientFill (fill);
        g.fillPath (p);
        g.setColour (colours::ref.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    // ---- your content, post-carve (purple) + carved band (magenta) ---------------
    // The curves live on a decibel axis (60 dB spans the full height). carveCurve is a
    // LINEAR gain reduction, so it cannot be multiplied into a dB coordinate — doing that
    // drew a dip several times deeper than reality. Convert the reduction to dB and drop
    // the level by that fraction of the axis, which is what the ear and the meters agree
    // the carve actually is.
    std::array<float, kPts> postCurve {};
    for (int i = 0; i < kPts; ++i)
    {
        const float g = juce::jlimit (0.001f, 1.0f, 1.0f - carveCurve[(size_t) i]);
        const float reductionNorm = (-20.0f * std::log10 (g)) / 60.0f;
        postCurve[(size_t) i] = juce::jmax (0.0f, mainCurve[(size_t) i] - reductionNorm);
    }

    {
        const auto p = areaPath (postCurve);
        juce::ColourGradient fill (colours::bus.withAlpha (0.30f), plot.getX(), cy,
                                   colours::bus.withAlpha (0.05f), plot.getX(), plot.getY(), false);
        g.setGradientFill (fill);
        g.fillPath (p);
        g.setColour (colours::bus.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    {
        const auto top = ribbonPath (mainCurve, postCurve, -1.0f);
        const auto bot = ribbonPath (mainCurve, postCurve,  1.0f);
        g.setColour (colours::carved.withAlpha (0.40f));
        g.fillPath (top);
        g.fillPath (bot);
        if (! eco)
        {
            g.setColour (colours::carved.withAlpha (0.55f));
            g.strokePath (top, juce::PathStrokeType (1.0f));
            g.strokePath (bot, juce::PathStrokeType (1.0f));
        }
    }

    // ---- status pill ---------------------------------------------------------------
    const int state = processor.uiState.load();
    const float carvedDb = processor.uiCarvedDb.load();
    const bool autoOn = processor.apvts.getRawParameterValue ("autocal")->load() > 0.5f;

    juce::Colour sc = state == 2 ? colours::active
                                 : (state == 1 ? colours::standby
                                 : (state == 3 ? juce::Colour (0xffe8564c)
                                               : juce::Colour (0xff5a5d66)));
    juce::String st = state == 2 ? "CARVING " + juce::String (carvedDb, 1) + " dB"
                                 : (state == 1 ? "STANDBY"
                                 : (state == 3 ? "SIDECHAIN NOT ROUTED" : "NO SIDECHAIN"));
    if (autoOn && state == 2)
        st = "AUTO  " + st;

    const auto pill = juce::Rectangle<float> (176.0f, 22.0f)
                          .withPosition (plot.getRight() - 176.0f, plot.getY());
    g.setColour (juce::Colour (0xff101116).withAlpha (0.9f));
    g.fillRoundedRectangle (pill, 11.0f);
    g.setColour (sc.withAlpha (0.5f));
    g.drawRoundedRectangle (pill, 11.0f, 1.0f);
    g.setColour (sc);
    g.fillEllipse (pill.getX() + 9.0f, pill.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.80f));
    g.setFont (juce::Font (juce::FontOptions (9.5f)).withExtraKerningFactor (0.14f));
    g.drawText (st, pill.withTrimmedLeft (20.0f).toNearestInt(), juce::Justification::centred);

    // ---- what your speakers cannot tell you ----------------------------------------
    // Laptop speakers roll off below ~250 Hz, which is exactly where masking lives.
    // The plugin measures the signal, not the room, so show that range as a number.
    if (state == 2)
    {
        const float lowDb = processor.uiLowCarvedDb.load();
        const float depth = processor.uiAppliedDepth.load();

        const auto sub = juce::Rectangle<float> (176.0f, 32.0f)
                             .withPosition (plot.getRight() - 176.0f, plot.getY() + 26.0f);

        // Backing plate — the carved ribbons run underneath and would swallow the text.
        // Expanded vertically only, so its right edge still lines up with the pill above.
        g.setColour (juce::Colour (0xff101116).withAlpha (0.82f));
        g.fillRoundedRectangle (sub.expanded (0.0f, 3.0f), 6.0f);

        // Two separately captioned figures. Printing the global depth under a "below
        // 250 Hz" heading would read as a sub-250 Hz number, which it is not.
        auto row = [&g, &sub] (float dy, const juce::String& caption,
                               const juce::String& value, juce::Colour valueColour)
        {
            const auto r = sub.withHeight (14.0f).translated (0.0f, dy);
            g.setColour (juce::Colours::white.withAlpha (0.32f));
            g.setFont (juce::Font (juce::FontOptions (8.0f)).withExtraKerningFactor (0.16f));
            g.drawText (caption, r.toNearestInt(), juce::Justification::centredLeft);
            g.setColour (valueColour);
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawText (value, r.toNearestInt(), juce::Justification::centredRight);
        };

        row (0.0f,  "BELOW 250 Hz  (LAPTOP-BLIND)", juce::String (lowDb, 1) + " dB",
             colours::carved.withAlpha (0.85f));
        row (15.0f, autoOn ? "AUTO DEPTH" : "DEPTH",
             juce::String (juce::roundToInt (depth * 100.0f)) + " %",
             colours::active.withAlpha (0.85f));
    }


    // ---- routing hints -------------------------------------------------------------
    if (state == 0 || state == 3)
    {
        const bool notRouted = (state == 3);

        g.setColour ((notRouted ? juce::Colour (0xffe8564c) : juce::Colours::white)
                         .withAlpha (notRouted ? 0.90f : 0.38f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (notRouted ? "The sidechain input is receiving this track, not your priority sound"
                              : "Route your lead / vocal / kick into the sidechain input",
                    plot.toNearestInt(), juce::Justification::centred);

        g.setColour (juce::Colours::white.withAlpha (notRouted ? 0.55f : 0.22f));
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText (notRouted
                        ? "Assign your send to the Priority A input in the host's plug-in routing   (FL Studio: Processing > Connections)"
                        : "Send your lead / vocal / kick to this plug-in's Priority A sidechain input",
                    plot.translated (0.0f, 22.0f).toNearestInt(), juce::Justification::centred);
    }
}

//==============================================================================
// CreditsOverlay
//==============================================================================
namespace
{
    /** Artwork arrives in whatever format it was exported in. A logo that carries no
        transparency was drawn for a light background and would sit on this dark panel as
        a glaring white tile, so key it on luminance instead: alpha carries the artwork
        and white carries the colour. Ink becomes solid, mid greys stay translucent, and
        the original paper disappears. Anything that already ships real transparency is
        left exactly as the artist made it. */
    juce::Image prepareLogoForDarkUI (const juce::Image& src)
    {
        if (! src.isValid())
            return src;

        if (src.hasAlphaChannel())
        {
            const int sx = juce::jmax (1, src.getWidth()  / 48);
            const int sy = juce::jmax (1, src.getHeight() / 48);

            for (int y = 0; y < src.getHeight(); y += sy)
                for (int x = 0; x < src.getWidth(); x += sx)
                    if (src.getPixelAt (x, y).getAlpha() < 16)
                        return src;                  // genuinely transparent already
        }

        // Work at panel resolution: these files are up to 2268 px wide and the credits
        // card shows them at ~140, so converting the originals would stall opening the
        // editor on a modest machine for no visible gain.
        juce::Image work = src;
        const int maxSide = juce::jmax (src.getWidth(), src.getHeight());
        if (maxSide > 512)
            work = src.rescaled (src.getWidth()  * 512 / maxSide,
                                 src.getHeight() * 512 / maxSide,
                                 juce::Graphics::highResamplingQuality);

        juce::Image out (juce::Image::ARGB, work.getWidth(), work.getHeight(), true);
        const juce::Image::BitmapData in (work, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < work.getHeight(); ++y)
        {
            for (int x = 0; x < work.getWidth(); ++x)
            {
                const auto c = in.getPixelColour (x, y);
                const float lum = 0.299f * c.getFloatRed()
                                + 0.587f * c.getFloatGreen()
                                + 0.114f * c.getFloatBlue();

                // Headroom under pure white keeps JPEG ringing from leaving a grey haze
                // around the artwork once the background is gone.
                dst.setPixelColour (x, y, juce::Colours::white
                                              .withAlpha (juce::jlimit (0.0f, 1.0f,
                                                              (0.94f - lum) / 0.72f)));
            }
        }

        return out;
    }
}

void CreditsOverlay::ensureLogosLoaded()
{
    if (logosLoaded)
        return;
    logosLoaded = true;

    // Decoding two multi-megapixel images and keying them for the dark UI is not free.
    // The panel starts hidden and most sessions never open it, so this runs on the first
    // show instead of at every editor construction — the editor opens instantly and the
    // artwork is not held resident until it is actually needed.
    jedexLogo  = prepareLogoForDarkUI (
                     juce::ImageFileFormat::loadFrom (BinaryData::jedex_logo_png,
                                                      (size_t) BinaryData::jedex_logo_pngSize));
    bigiceLogo = prepareLogoForDarkUI (
                     juce::ImageFileFormat::loadFrom (BinaryData::bigice_logo_png,
                                                      (size_t) BinaryData::bigice_logo_pngSize));
}

void CreditsOverlay::visibilityChanged()
{
    if (isVisible())
        ensureLogosLoaded();
}

void CreditsOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.78f));

    cardArea = getLocalBounds().withSizeKeepingCentre (480, 300);
    const auto card = cardArea.toFloat();
    g.setColour (juce::Colour (0xff121318));
    g.fillRoundedRectangle (card, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (15.0f)).withExtraKerningFactor (0.30f));
    g.drawText ("CREDITS", cardArea.getX(), cardArea.getY() + 18, cardArea.getWidth(), 20,
                juce::Justification::centred);

    closeArea = { cardArea.getRight() - 34, cardArea.getY() + 12, 22, 22 };
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText ("x", closeArea, juce::Justification::centred);

    const int colW = 200;
    jedexArea  = { cardArea.getX() + 30,               cardArea.getY() + 52, colW, 190 };
    bigiceArea = { cardArea.getRight() - 30 - colW,    cardArea.getY() + 52, colW, 190 };

    auto drawArtist = [&g] (const juce::Rectangle<int>& area, const juce::Image& logo,
                            const juce::String& name)
    {
        const auto logoBox = area.withHeight (130).reduced (30, 0).toFloat();
        if (logo.isValid())
            g.drawImageWithin (logo, (int) logoBox.getX(), (int) logoBox.getY(),
                               (int) logoBox.getWidth(), (int) logoBox.getHeight(),
                               juce::RectanglePlacement::centred);

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.16f));
        g.drawText (name, area.withY (area.getY() + 136).withHeight (18),
                    juce::Justification::centred);

        g.setColour (colours::ref.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)).withExtraKerningFactor (0.20f));
        g.drawText ("OPEN SPOTIFY", area.withY (area.getY() + 158).withHeight (14),
                    juce::Justification::centred);
    };

    drawArtist (jedexArea, jedexLogo, "JeDEx");
    drawArtist (bigiceArea, bigiceLogo, "BIG ICE");

    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)).withExtraKerningFactor (0.20f));
    g.drawText ("CARVE  -  by JeDEx x Big Ice",
                cardArea.withY (cardArea.getBottom() - 26).withHeight (14),
                juce::Justification::centred);
}

void CreditsOverlay::mouseUp (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    if (jedexArea.contains (pos))
        juce::URL (kJedexSpotifyUrl).launchInDefaultBrowser();
    else if (bigiceArea.contains (pos))
        juce::URL (kBigIceSpotifyUrl).launchInDefaultBrowser();
    else if (closeArea.expanded (6).contains (pos) || ! cardArea.contains (pos))
        setVisible (false);
}

//==============================================================================
// CarveAudioProcessorEditor
//==============================================================================
CarveAudioProcessorEditor::CarveAudioProcessorEditor (CarveAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), spectrum (p)
{
    setOpaque (true);

    addAndMakeVisible (spectrum);

    auto initKnob = [this] (juce::Slider& s, const juce::String& name, const juce::String& paramId,
                            std::unique_ptr<SliderAttachment>& att)
    {
        s.setName (name);
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setLookAndFeel (&lookAndFeel);
        s.setBufferedToImage (true);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAttachment> (processor.apvts, paramId, s);
    };

    initKnob (amountKnob, "AMOUNT",     "amount", amountAtt);
    initKnob (smoothKnob, "SMOOTHNESS", "smooth", smoothAtt);
    initKnob (mixKnob,    "MIX",        "mix",    mixAtt);
    initKnob (outputKnob, "OUTPUT",     "output", outputAtt);
    amountKnob.setDoubleClickReturnValue (true, 0.5);

    ecoToggle.setLookAndFeel (&lookAndFeel);
    ecoToggle.setBufferedToImage (true);
    addAndMakeVisible (ecoToggle);
    ecoAtt = std::make_unique<ButtonAttachment> (processor.apvts, "eco", ecoToggle);

    autoToggle.setLookAndFeel (&lookAndFeel);
    autoToggle.setBufferedToImage (true);
    addAndMakeVisible (autoToggle);
    autoAtt = std::make_unique<ButtonAttachment> (processor.apvts, "autocal", autoToggle);

    // The big knob switches meaning between depth and bias, so refresh it (and only it)
    // when the mode changes — never per frame, the metallic body is expensive to draw.
    // onStateChange also fires on hover and press, so bail out unless the mode really
    // changed — repainting the metallic knob on every mouse-over is not free.
    auto refreshAutoMode = [this]
    {
        const bool on = autoToggle.getToggleState();
        if (on == lookAndFeel.autoMode && amountKnob.getName().isNotEmpty())
            return;

        lookAndFeel.autoMode = on;
        amountKnob.setName (on ? "AUTO TRIM" : "AMOUNT");
        amountKnob.repaint();
    };
    autoToggle.onStateChange = refreshAutoMode;

    amountKnob.setName ({});      // force the first refresh to take effect
    refreshAutoMode();

    creditsButton.setLookAndFeel (&lookAndFeel);
    creditsButton.onClick = [this] { credits.setVisible (true); credits.toFront (false); };
    addAndMakeVisible (creditsButton);

    addChildComponent (credits);

    setSize (carve::kEditorWidth, carve::kEditorHeight);
}

CarveAudioProcessorEditor::~CarveAudioProcessorEditor()
{
    amountKnob.setLookAndFeel (nullptr);
    smoothKnob.setLookAndFeel (nullptr);
    mixKnob.setLookAndFeel (nullptr);
    outputKnob.setLookAndFeel (nullptr);
    ecoToggle.setLookAndFeel (nullptr);
    autoToggle.setLookAndFeel (nullptr);
    creditsButton.setLookAndFeel (nullptr);
}

void CarveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // header
    g.setColour (juce::Colours::white.withAlpha (0.94f));
    g.setFont (juce::Font (juce::FontOptions (26.0f)).boldened().withExtraKerningFactor (0.10f));
    g.drawText ("CARVE", 24, 12, 130, 30, juce::Justification::centredLeft);

    g.setColour (colours::ref.withAlpha (0.80f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)).withExtraKerningFactor (0.12f));
    g.drawText ("by JeDEx x Big Ice", 152, 20, 160, 16, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.30f));
    g.setFont (juce::Font (juce::FontOptions (8.5f)).withExtraKerningFactor (0.34f));
    g.drawText ("CONTEXT-AWARE HARMONIC CARVING", 24, 42, 300, 12,
                juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (20.0f, 58.0f, (float) getWidth() - 20.0f, 58.0f, 1.0f);
}

void CarveAudioProcessorEditor::resized()
{
    spectrum.setBounds (20, 64, getWidth() - 40, 288);
    creditsButton.setBounds (getWidth() - 124, 16, 100, 26);

    autoToggle.setBounds (28, 398, 190, 30);
    ecoToggle.setBounds (28, 442, 190, 30);

    // Every knob shares the same bottom edge so the four captions sit on one baseline —
    // the big AMOUNT knob is taller, it just starts higher. Even 26 px gaps, and the
    // group is centred in the space left of the toggles rather than in the window.
    smoothKnob.setBounds (256, 376, 100, 134);
    amountKnob.setBounds (382, 354, 144, 156);
    mixKnob.setBounds    (552, 376, 100, 134);
    outputKnob.setBounds (678, 376, 100, 134);

    credits.setBounds (getLocalBounds());
}
