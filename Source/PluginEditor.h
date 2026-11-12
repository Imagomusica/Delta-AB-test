#pragma once
#include "PluginProcessor.h"
#include <memory>

// Frequency-response display with draggable band nodes and a spectrum analyzer.
class EqDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EqDisplay (MasteringEQAudioProcessor&);
    ~EqDisplay() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override { plot = getLocalBounds().toFloat().reduced (40.0f, 20.0f); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    std::function<void (int)> onSelect;
    void setSelected (int i) { selected = i; repaint(); }
    int getSelected() const { return selected; }

private:
    void timerCallback() override;
    float freqToX (double f) const;
    double xToFreq (float x) const;
    float dbToY (double db) const;
    double yToDb (float y) const;
    int hitTest (juce::Point<float>) const;
    void setParam (int band, const char* name, float value);
    void updateAnalyzer();

    MasteringEQAudioProcessor& proc;
    juce::Rectangle<float> plot;
    int selected = 0;
    int dragging = -1;

    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, fftSize / 2> spectrum {};
};

class MasteringEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasteringEQAudioProcessorEditor (MasteringEQAudioProcessor&);
    ~MasteringEQAudioProcessorEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectBand (int i);
    void updateControlVisibility();
    void updateBandButtons();

    MasteringEQAudioProcessor& proc;
    EqDisplay display;

    // global controls
    juce::ComboBox modeBox, qualityBox;
    juce::Slider outSlider;
    juce::ToggleButton bypassButton { "Bypass" };
    juce::Label latencyLabel, titleLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, qualityAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    // band selector + per-band controls
    std::array<juce::TextButton, eqd::kMaxBands> bandButtons;
    juce::ToggleButton onButton { "On" };
    juce::ComboBox typeBox, chanBox, slopeBox;
    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Label freqLabel, gainLabel, qLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt, chanAtt, slopeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtt, gainAtt, qAtt;
    int currentBand = 0;
    int lastLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringEQAudioProcessorEditor)
};
