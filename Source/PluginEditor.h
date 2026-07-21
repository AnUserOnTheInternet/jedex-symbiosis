#pragma once

#include "PluginProcessor.h"

namespace symbiosis
{
    inline constexpr int   kEditorWidth  = 640;
    inline constexpr int   kEditorHeight = 660;
    inline constexpr int   kKnobSize     = 200;
    inline constexpr float kInnerRadius  = 112.0f;   // radar ring starts outside the knob

    namespace colours
    {
        inline const juce::Colour background  { 0xff0a0a0d };
        inline const juce::Colour backgroundHi{ 0xff101116 };
        inline const juce::Colour neonBlue    { 0xff00d4ff };
        inline const juce::Colour neonPurple  { 0xffb44cff };
        inline const juce::Colour carveMagenta{ 0xffe14ce8 };
    }
}

//==============================================================================
/** Metallic rotary look for the single 'Symbiosis Amount' knob. */
class SymbiosisLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};

//==============================================================================
/**
    Circular radar visualiser.

    Angle  = log-frequency (30 Hz .. 16 kHz, clockwise from 12 o'clock).
    Radius = level. Neon blue ring = main layer, purple ring = combined
    sidechain layers (pre-carve), magenta radial glow = spectrum currently
    being carved out of the sidechains.
*/
class RadarVisualizer : public juce::Component,
                        private juce::Timer
{
public:
    explicit RadarVisualizer (JeDExSymbiosisAudioProcessor&);
    ~RadarVisualizer() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    JeDExSymbiosisAudioProcessor& processor;

    static constexpr int kPoints = 240;
    std::array<float, kPoints> mainCurve {}, sideCurve {}, carveCurve {};

    float sweepPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RadarVisualizer)
};

//==============================================================================
class JeDExSymbiosisAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit JeDExSymbiosisAudioProcessorEditor (JeDExSymbiosisAudioProcessor&);
    ~JeDExSymbiosisAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JeDExSymbiosisAudioProcessor& processor;

    SymbiosisLookAndFeel lookAndFeel;
    RadarVisualizer radar;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JeDExSymbiosisAudioProcessorEditor)
};
