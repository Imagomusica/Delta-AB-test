#pragma once
#include "PluginProcessor.h"
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// Themes: White 9016, Anthracite 7016, Black 9005
struct Palette
{
    juce::Colour bg, fg, dim, ring;
};
Palette paletteFor (int theme);

class DuneLnF : public juce::LookAndFeel_V4
{
public:
    void setPalette (const Palette& p);
    juce::Font font (float size, int style = juce::Font::plain) const;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

private:
    Palette pal = paletteFor (2);
};

// ---------------------------------------------------------------------------
// Nagra-style toggle: lever in a recessed round bezel (up = ON, down = OFF), label beside it
class NagraSwitch : public juce::Button
{
public:
    NagraSwitch (const juce::String& text, const Palette& p) : juce::Button (text), pal (p) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void setLabel (const juce::String& s) { label = s; repaint(); }
    juce::String label;
private:
    const Palette& pal;
};

// ---------------------------------------------------------------------------
// Split compressor panel: one band (low cut + high cut with slopes), pre/post EQ, stereo or mid/side
class CompPanel : public juce::Component
{
public:
    CompPanel (MasteringEQAudioProcessor&, const Palette&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    MasteringEQAudioProcessor& proc;
    const Palette& pal;
    NagraSwitch onBtn, postBtn, minBtn;
    juce::ComboBox msBox, sloBox, shiBox;
    juce::Slider knob[8];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt[8];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> msAtt, sloAtt, shiAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt, postAtt, minAtt;
};

// ---------------------------------------------------------------------------
// Sand-dune field: dotted ridges that follow the EQ curve and the live spectrum, plus the modulometer
class DuneField : public juce::Component, private juce::Timer
{
public:
    DuneField (MasteringEQAudioProcessor&, const Palette&);
    ~DuneField() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Particle { float u, jy, h; int layer; };
    void timerCallback() override;
    void updateAnalyzer();
    void drawMeters (juce::Graphics&);

    MasteringEQAudioProcessor& proc;
    const Palette& pal;
    std::vector<Particle> particles;
    static constexpr int kPoints = 512;
    static constexpr int kLayers = 16;
    std::array<float, kPoints> dbL {}, dbM {}, dbS {}, specN {}, phaseDeg {};
    float level = 0.0f, transient = 0.0f, corr = 0.0f, gonioPeak = 0.1f;
    bool demo = false;
    std::vector<juce::Point<float>> gonio;
    bool anyMS = false;
    float t = 0.0f;
    juce::Rectangle<float> meterPanel;

    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, fftSize / 2> spectrum {};
};

// ---------------------------------------------------------------------------
class BandStrip : public juce::Component
{
public:
    BandStrip (MasteringEQAudioProcessor&, int index, const Palette&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    void setChannel (int idx);

    MasteringEQAudioProcessor& proc;
    const int band;
    const Palette& pal;
    bool bandOn = true;
    bool isPass = false, hasGain = true;

    juce::TextButton numBtn, stBtn, mBtn, sBtn;
    juce::ComboBox typeBox, slopeBox, famBox;
    juce::Slider freqKnob, gainKnob, qKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt, slopeAtt, famAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtt, gainAtt, qAtt;
};

// ---------------------------------------------------------------------------
class DuneContent : public juce::Component, private juce::Timer
{
public:
    explicit DuneContent (MasteringEQAudioProcessor&);
    ~DuneContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyTheme();
    void setParamIndex (const char* id, int idx);

    MasteringEQAudioProcessor& proc;
    DuneLnF lnf;
    Palette palette = paletteFor (2);

    juce::TextButton modeBtn[3];
    juce::ComboBox qualityBox, tuningBox, themeBox;
    NagraSwitch curvesBtn, curvesTopBtn, tiltBtn, bypassBtn, lookBtn;
    juce::ComboBox osBox;
    NagraSwitch lineBtn;
    juce::Slider lineSlider, linePivot;
    CompPanel comp;
    juce::TextButton plusBtn { "+" }, minusBtn { "-" };
    juce::Slider tiltSlider, outSlider;
    DuneField field;
    std::vector<std::unique_ptr<BandStrip>> strips;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt, tuningAtt, themeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tiltAtt, bypassAtt, curvesAtt, curvesTopAtt, lookAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lineAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lineSAtt, linePAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tiltSliderAtt, outAtt;
    juce::String footerText;
    int lastTheme = -1;
};

class MasteringEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MasteringEQAudioProcessorEditor (MasteringEQAudioProcessor&);
    void resized() override;

private:
    DuneContent content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringEQAudioProcessorEditor)
};
