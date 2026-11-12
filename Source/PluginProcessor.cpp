#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const char* kBandTypes[]  = { "Cloche", "Shelf grave", "Shelf aigu", "Coupe-bas", "Coupe-haut", "Notch" };
const char* kChannels[]   = { "ST", "L", "R", "M", "S" };
const int   kSlopes[]     = { 6, 12, 18, 24, 36, 48, 72, 96 };
const double kDefFreq[]   = { 20, 100, 200, 800, 2500, 6000, 10000, 30000 };
const int    kDefType[]   = { 3, 1, 0, 0, 0, 0, 2, 4 };
const double kTuning[]    = { 415, 432, 435, 438, 440, 442, 443, 444 };

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
    designThread.stopThread (5000);
}

juce::AudioProcessorValueTreeState::ParameterLayout MasteringEQAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 2 }, "Phase mode",
                                                        StringArray { "Lineaire", "Naturelle", "Minimale" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "quality", 2 }, "Resolution",
                                                        StringArray { "Court 0.35 s", "Moyen 0.7 s", "Standard 1.4 s", "Long 2.8 s" }, 2));
    StringArray tunings;
    for (auto t : kTuning) tunings.add ("La " + String ((int) t));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "tuning", 1 }, "Reference pitch", tunings, 5));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "theme", 1 }, "Theme",
                                                        StringArray { "White 9016", "Anthracite 7016", "Black 9005" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output gain",
                                                       NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "curves_on", 1 }, "Show EQ curves", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "curves_top", 1 }, "Curves above sand", true));

    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "tilt_on", 1 }, "Tilt On", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tilt_db", 1 }, "Tilt",
                                                       NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tilt_freq", 1 }, "Tilt pivot",
                                                       logRange (100.0f, 5000.0f), 650.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));

    StringArray types, chans, slopes;
    for (auto* t : kBandTypes) types.add (t);
    for (auto* c : kChannels) chans.add (c);
    for (auto s : kSlopes) slopes.add (String (s) + " dB/oct");

    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { pid (i, "on"), 1 },    n + "On", i < 7));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "type"), 1 },  n + "Type", types, kDefType[i]));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid (i, "freq"), 1 },  n + "Frequency",
                                                            logRange (3.0f, 30000.0f), (float) kDefFreq[i],
                                                            AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float v, int)
                                                                    { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, 1) + " Hz"; })
                                                                .withValueFromStringFunction ([] (const String& t)
                                                                    { const auto s = t.trim(); float v = s.getFloatValue();
                                                                      return s.containsIgnoreCase ("k") ? v * 1000.0f : v; })));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid (i, "gain"), 1 },  n + "Gain",
                                                            NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                            AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid (i, "q"), 1 },     n + "Q",
                                                            logRange (0.1f, 20.0f), 0.71f));
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

eqd::Tilt MasteringEQAudioProcessor::readTilt() const
{
    eqd::Tilt t;
    t.on = apvts.getRawParameterValue ("tilt_on")->load() > 0.5f;
    t.db = apvts.getRawParameterValue ("tilt_db")->load();
    t.pivot = apvts.getRawParameterValue ("tilt_freq")->load();
    return t;
}

eqd::PhaseMode MasteringEQAudioProcessor::readMode() const
{
    return (eqd::PhaseMode) juce::jlimit (0, 2, (int) std::lround (apvts.getRawParameterValue ("mode")->load()));
}

double MasteringEQAudioProcessor::readTuning() const
{
    return kTuning[juce::jlimit (0, 7, (int) std::lround (apvts.getRawParameterValue ("tuning")->load()))];
}

void MasteringEQAudioProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "out" || id == "bypass" || id == "tuning" || id == "theme") return;   // no new filter needed
    designThread.trigger();
}

void MasteringEQAudioProcessor::runDesign()
{
    const double fs = currentSampleRate.load();
    if (fs <= 0.0) return;

    eqd::Band bands[eqd::kMaxBands];
    for (int i = 0; i < eqd::kMaxBands; ++i) bands[i] = readBand (i);
    const int quality = (int) std::lround (apvts.getRawParameterValue ("quality")->load());
    const auto res = eqd::design (bands, eqd::kMaxBands, readTilt(), fs, quality, readMode());

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
    tpScratch.setSize (2, maxBlock, false, false, true);

    juce::dsp::ProcessSpec dspec { sampleRate, (juce::uint32) maxBlock, 2 };
    dryDelay.prepare (dspec);
    dryDelay.reset();

    tpOversampler.initProcessing ((size_t) maxBlock);
    tpOversampler.reset();
    vuCoef = 1.0f - std::exp (-1.0f / (0.065f * (float) sampleRate));
    ppmAttack = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    ppmDecay = std::pow (10.0f, -1.0f / (1.7f * (float) sampleRate));
    rmsCoef = 1.0f - std::exp (-1.0f / (0.3f * (float) sampleRate));
    for (int c = 0; c < 2; ++c) { vuState[c] = ppmState[c] = rmsState[c] = tpHold[c] = 0.0f; }

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

void MasteringEQAudioProcessor::updateMeters (const juce::AudioBuffer<float>& buffer, int start, int n, int numCh)
{
    if (resetTruePeak.exchange (false)) tpHold[0] = tpHold[1] = 0.0f;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* x = buffer.getReadPointer (ch) + start;
        float vu = vuState[ch], ppm = ppmState[ch], r2 = rmsState[ch];
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (x[i]);
            vu += (a - vu) * vuCoef;
            if (a > ppm) ppm += (a - ppm) * ppmAttack; else ppm *= ppmDecay;
            r2 += (a * a - r2) * rmsCoef;
        }
        vuState[ch] = vu; ppmState[ch] = ppm; rmsState[ch] = r2;
        meters[ch].vu.store (juce::Decibels::gainToDecibels (vu * 1.1107f, -120.0f));
        meters[ch].ppm.store (juce::Decibels::gainToDecibels (ppm, -120.0f));
        meters[ch].rms.store (10.0f * std::log10 (std::max (r2, 1.0e-12f)));
    }

    // true peak: 4x oversampled
    for (int c = 0; c < 2; ++c)
        tpScratch.copyFrom (c, 0, buffer, std::min (c, numCh - 1), start, n);
    juce::dsp::AudioBlock<const float> in (tpScratch.getArrayOfReadPointers(), 2, (size_t) n);
    auto up = tpOversampler.processSamplesUp (in);
    for (int ch = 0; ch < numCh; ++ch)
    {
        float m = tpHold[ch];
        const float* u = up.getChannelPointer ((size_t) ch);
        for (size_t i = 0; i < up.getNumSamples(); ++i) m = std::max (m, std::abs (u[i]));
        tpHold[ch] = m;
        meters[ch].truePeak.store (juce::Decibels::gainToDecibels (m, -120.0f));
    }
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
            anaL[(size_t) pos] = buffer.getReadPointer (0)[start + i];
            anaR[(size_t) pos] = buffer.getReadPointer (numCh - 1)[start + i];
            anaPos.store ((pos + 1) % anaSize, std::memory_order_release);
        }

        updateMeters (buffer, start, n, numCh);
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
