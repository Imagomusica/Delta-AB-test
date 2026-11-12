#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const char* kBandTypes[]  = { "Bell", "Low shelf", "High shelf", "High-pass", "Low-pass", "Notch" };
const char* kChannels[]   = { "Stereo", "Left", "Right", "Mid", "Side" };
const int   kSlopes[]     = { 6, 12, 18, 24, 36, 48, 72, 96 };
const double kDefFreq[]   = { 30, 100, 300, 1000, 3000, 8000, 12000, 18000 };
const int    kDefType[]   = { 3, 1, 0, 0, 0, 0, 2, 4 };

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float n) { return s * std::pow (e / s, n); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             nullptr };
}
} // namespace

MasteringEQAudioProcessor::MasteringEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->paramID, this);
    designThread.startThread();
}

MasteringEQAudioProcessor::~MasteringEQAudioProcessor()
{
    cancelPendingUpdate();
    designThread.signalThreadShouldExit();
    designThread.notify();
    designThread.stopThread (3000);
}

juce::AudioProcessorValueTreeState::ParameterLayout MasteringEQAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Phase mode",
                                                        StringArray { "Natural (min. phase)", "Linear phase" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "quality", 1 }, "Resolution",
                                                        StringArray { "Standard", "High", "Very high", "Maximum" }, 1));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output gain",
                                                       NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

    StringArray types, chans, slopes;
    for (auto* t : kBandTypes) types.add (t);
    for (auto* c : kChannels) chans.add (c);
    for (auto s : kSlopes) slopes.add (String (s) + " dB/oct");

    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { pid (i, "on"), 1 },    n + "On", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "type"), 1 },  n + "Type", types, kDefType[i]));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid (i, "freq"), 1 },  n + "Frequency",
                                                            logRange (10.0f, 30000.0f), (float) kDefFreq[i],
                                                            AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float v, int)
                                                                    { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, 1) + " Hz"; })
                                                                .withValueFromStringFunction ([] (const String& t)
                                                                    { const auto s = t.trim(); float v = s.getFloatValue();
                                                                      return s.containsIgnoreCase ("k") ? v * 1000.0f : v; })));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid (i, "gain"), 1 },  n + "Gain",
                                                            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                            AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid (i, "q"), 1 },     n + "Q",
                                                            logRange (0.1f, 20.0f), 1.0f));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "ch"), 1 },    n + "Channel", chans, 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "slope"), 1 }, n + "Slope", slopes, 1));
    }
    return layout;
}

int MasteringEQAudioProcessor::slopeFromIndex (int idx)
{
    return kSlopes[juce::jlimit (0, 7, idx)];
}

eqd::Band MasteringEQAudioProcessor::readBand (int i) const
{
    auto get = [this, i] (const char* n) { return apvts.getRawParameterValue (pid (i, n))->load(); };
    eqd::Band b;
    b.on = get ("on") > 0.5f;
    b.type = (eqd::BandType) juce::jlimit (0, 5, (int) std::lround (get ("type")));
    b.freq = get ("freq");
    b.gainDb = get ("gain");
    b.q = get ("q");
    b.ch = (eqd::Channel) juce::jlimit (0, 4, (int) std::lround (get ("ch")));
    b.slopeDbOct = slopeFromIndex ((int) std::lround (get ("slope")));
    return b;
}

eqd::PhaseMode MasteringEQAudioProcessor::readMode() const
{
    return apvts.getRawParameterValue ("mode")->load() > 0.5f ? eqd::PhaseMode::Linear : eqd::PhaseMode::Natural;
}

void MasteringEQAudioProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "out" || id == "bypass") return;   // handled in the audio thread
    designThread.trigger();
}

void MasteringEQAudioProcessor::runDesign()
{
    const double fs = currentSampleRate.load();
    if (fs <= 0.0) return;

    eqd::Band bands[eqd::kMaxBands];
    for (int i = 0; i < eqd::kMaxBands; ++i) bands[i] = readBand (i);
    const int quality = (int) std::lround (apvts.getRawParameterValue ("quality")->load());
    const auto res = eqd::design (bands, eqd::kMaxBands, fs, quality, readMode());

    auto load = [&] (juce::dsp::Convolution& c, const std::vector<float>& v)
    {
        juce::AudioBuffer<float> b (1, (int) v.size());
        std::copy (v.begin(), v.end(), b.getWritePointer (0));
        c.loadImpulseResponse (std::move (b), fs, juce::dsp::Convolution::Stereo::no,
                               juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    };

    load (convLL, res.ll);
    load (convRR, res.rr);
    if (res.crossActive)
    {
        load (convLR, res.lr);
        load (convRL, res.rl);
    }
    else
    {
        std::vector<float> zeros (64, 0.0f);
        load (convLR, zeros);
        load (convRL, zeros);
    }
    crossActive = res.crossActive;
    latencyTarget = res.latencySamples;
    triggerAsyncUpdate();
}

void MasteringEQAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (latencyTarget.load());
}

void MasteringEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (samplesPerBlock, 64);
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 1 };
    for (auto* c : { &convLL, &convLR, &convRL, &convRR }) { c->prepare (spec); c->reset(); }

    for (auto* b : { &bufLL, &bufLR, &bufRL, &bufRR }) b->setSize (1, maxBlock, false, false, true);
    dryBuf.setSize (2, maxBlock, false, false, true);

    juce::dsp::ProcessSpec dspec { sampleRate, (juce::uint32) maxBlock, 2 };
    dryDelay.prepare (dspec);
    dryDelay.reset();

    gainSmooth.reset (sampleRate, 0.03);
    bypassSmooth.reset (sampleRate, 0.03);
    gainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out")->load()));
    bypassSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue ("bypass")->load() > 0.5f ? 1.0f : 0.0f);

    runDesign();   // synchronous first design so a filter is available immediately
}

bool MasteringEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void MasteringEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = std::min (buffer.getNumChannels(), 2);
    const int total = buffer.getNumSamples();
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, total);

    gainSmooth.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out")->load()));
    bypassSmooth.setTargetValue (apvts.getRawParameterValue ("bypass")->load() > 0.5f ? 1.0f : 0.0f);
    dryDelay.setDelay ((float) latencyTarget.load());
    const bool cross = crossActive.load() && numCh == 2;

    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = std::min (maxBlock, total - start);

        // dry path, delayed by the filter latency so that bypass is phase-aligned
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = dryBuf.getWritePointer (ch);
            const auto* in = buffer.getReadPointer (ch) + start;
            for (int i = 0; i < n; ++i)
            {
                dryDelay.pushSample (ch, in[i]);
                d[i] = dryDelay.popSample (ch);
            }
        }

        auto run = [n] (juce::dsp::Convolution& c, juce::AudioBuffer<float>& b)
        {
            juce::dsp::AudioBlock<float> blk (b);
            auto sub = blk.getSubBlock (0, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctx (sub);
            c.process (ctx);
        };

        bufLL.copyFrom (0, 0, buffer, 0, start, n);
        run (convLL, bufLL);
        if (numCh == 2)
        {
            bufRR.copyFrom (0, 0, buffer, 1, start, n);
            run (convRR, bufRR);
            if (cross)
            {
                bufLR.copyFrom (0, 0, buffer, 1, start, n);   // R -> L
                bufRL.copyFrom (0, 0, buffer, 0, start, n);   // L -> R
                run (convLR, bufLR);
                run (convRL, bufRL);
                bufLL.addFrom (0, 0, bufLR, 0, 0, n);
                bufRR.addFrom (0, 0, bufRL, 0, 0, n);
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const float g = gainSmooth.getNextValue();
            const float bp = bypassSmooth.getNextValue();
            float mono = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float wet = (ch == 0 ? bufLL.getReadPointer (0)[i] : bufRR.getReadPointer (0)[i]) * g;
                const float y = wet * (1.0f - bp) + dryBuf.getReadPointer (ch)[i] * bp;
                buffer.getWritePointer (ch)[start + i] = y;
                mono += y;
            }
            const int pos = anaPos.load (std::memory_order_relaxed);
            anaBuf[(size_t) pos] = mono / (float) numCh;
            anaPos.store ((pos + 1) % anaSize, std::memory_order_release);
        }
    }
}

juce::AudioProcessorEditor* MasteringEQAudioProcessor::createEditor()
{
    return new MasteringEQAudioProcessorEditor (*this);
}

void MasteringEQAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void MasteringEQAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasteringEQAudioProcessor();
}
