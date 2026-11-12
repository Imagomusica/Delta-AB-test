#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "EqDesign.h"

class MasteringEQAudioProcessor : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::AsyncUpdater
{
public:
    MasteringEQAudioProcessor();
    ~MasteringEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- parameters
    static juce::String pid (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    eqd::Band readBand (int i) const;
    eqd::PhaseMode readMode() const;
    static int slopeFromIndex (int idx);
    juce::AudioProcessorValueTreeState apvts;

    // ---- info for the GUI
    int getLatencyForGui() const { return latencyTarget.load(); }
    static constexpr int anaSize = 16384;
    std::array<float, anaSize> anaBuf {};
    std::atomic<int> anaPos { 0 };

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void runDesign();

    struct DesignThread : public juce::Thread
    {
        explicit DesignThread (MasteringEQAudioProcessor& o) : juce::Thread ("EQ filter design"), owner (o) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (-1);
                if (threadShouldExit()) break;
                while (dirty.exchange (false) && ! threadShouldExit()) owner.runDesign();
            }
        }
        void trigger() { dirty = true; notify(); }
        MasteringEQAudioProcessor& owner;
        std::atomic<bool> dirty { false };
    };

    DesignThread designThread { *this };

    juce::dsp::Convolution convLL, convLR, convRL, convRR;
    juce::AudioBuffer<float> bufLL, bufLR, bufRL, bufRR, dryBuf;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 70000 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmooth, bypassSmooth;

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<bool> crossActive { false };
    std::atomic<int> latencyTarget { 0 };
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringEQAudioProcessor)
};
