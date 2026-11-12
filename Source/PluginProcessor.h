#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "EqDesign.h"
#include "Loudness.h"
#include "Resampler.h"
#include "HotConv.h"
#include <memory>

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
    double getTailLengthSeconds() const override { return 1.5; }

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
    eqd::Tilt readTilt() const;
    eqd::Tilt readLine() const;                      // straight-line tilt (dB/oct), last stage
    eqd::PhaseMode readMode() const;
    double readTuning() const;                       // reference pitch for note names (Hz)
    static int slopeFromIndex (int idx);
    juce::AudioProcessorValueTreeState apvts;

    // ---- info for the GUI
    int getLatencyForGui() const { return latencyTarget.load(); }
    static constexpr int anaSize = 16384;
    std::array<float, anaSize> anaBuf {};
    std::array<float, anaSize> anaL {}, anaR {};
    std::atomic<int> anaPos { 0 };

    // ---- modulometer (dBFS values, per channel)
    struct Meter
    {
        std::atomic<float> vu { -120.0f };      // VU ballistics (300 ms), dBFS of equivalent sine RMS
        std::atomic<float> ppm { -120.0f };     // quasi-peak (10 ms integration, 1.7 s for 20 dB return)
        std::atomic<float> rms { -120.0f };     // RMS over 300 ms
        std::atomic<float> truePeak { -120.0f };// 4x oversampled peak, held until reset
    };
    Meter meters[2];
    std::atomic<bool> resetTruePeak { false };
    LoudnessMeter inLoud, outLoud;                       // LUFS momentary / short-term / integrated
    std::atomic<float> compGrDb { 0.0f };                 // split compressor gain reduction (dB, >= 0)
    std::atomic<float> inRmsDb { -120.0f }, inPeakDb { -120.0f };   // input RMS (300 ms) and held sample peak

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void runDesign (int mask = 3);
    void updateLatency();
    void chain (float* xL, float* xR, int nn, bool cross);
    void designLine (double fs, double fsw, int quality);
    void runComp (float* L, float* R, int n);
    void updateMeters (const juce::AudioBuffer<float>& buffer, int start, int n, int numCh);

    struct DesignThread : public juce::Thread
    {
        explicit DesignThread (MasteringEQAudioProcessor& o) : juce::Thread ("EQ filter design"), owner (o) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (-1);
                if (threadShouldExit()) break;
                int m;
                while ((m = dirtyMask.exchange (0)) != 0 && ! threadShouldExit()) owner.runDesign (m);
            }
        }
        void trigger (int mask) { dirtyMask |= mask; notify(); }
        MasteringEQAudioProcessor& owner;
        std::atomic<int> dirtyMask { 0 };
    };

    DesignThread designThread { *this };

    HotConv convLL, convLR, convRL, convRR;
    juce::AudioBuffer<float> bufLL, bufLR, bufRL, bufRR, dryBuf, tpScratch, inBuf;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 300000 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> compDelay { 1100000 };
    // whole-chain oversampling (comp + EQ + tilt run at K x the host rate): 1x, 2x, 4x, 8x
    std::shared_ptr<SrcEngine> srcEng;                   // built on the design thread, used on the audio thread
    std::vector<std::shared_ptr<SrcEngine>> srcRetired;
    std::atomic<int> srcLatency { 0 };
    juce::AudioBuffer<float> workIn, dsBuf;
    std::atomic<int> activeK { 1 };
    int curK = 1;
    HotConv convCL, convCR, convTL, convTR;   // convT*: straight-line tilt, last stage                 // split-compressor band filter
    juce::AudioBuffer<float> compBufL, compBufR;
    std::atomic<int> latencyEq { 0 }, latencyComp { 0 }, latencyLine { 0 };
    float compState[2] { 0.0f, 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmooth, bypassSmooth;

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<bool> crossActive { false };
    std::atomic<int> latencyTarget { 0 };
    int maxBlock = 512;

    // metering state
    // true peak by polyphase windowed-sinc interpolation, any factor (4x ... 40x; 24x of 44.1 kHz = 1.06 MHz)
    static constexpr int kTpTaps = 24;
    int tpFactor = 0;
    std::vector<float> tpCoef;                 // [(K-1) phases][kTpTaps]
    std::vector<float> tpHist[2], tpTmp;
    void buildTruePeakKernel (int K);
    float inRmsState = 0.0f, inPeakHold = 0.0f;
    float vuState[2] { 0, 0 }, ppmState[2] { 0, 0 }, rmsState[2] { 0, 0 }, tpHold[2] { 0, 0 };
    float vuCoef = 0.0f, ppmAttack = 0.0f, ppmDecay = 1.0f, rmsCoef = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringEQAudioProcessor)
};
