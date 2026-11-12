#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const char* kBandTypes[]  = { "Cloche", "Shelf grave", "Shelf aigu", "Coupe-bas", "Coupe-haut", "Notch", "Passe-bande", "All-pass" };
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
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "tilt_line", 1 }, "Straight tilt On", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "line_db", 1 }, "Straight tilt slope",
                                                       NormalisableRange<float> (-6.0f, 6.0f, 0.05f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB/oct")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "line_freq", 1 }, "Straight tilt pivot",
                                                       logRange (100.0f, 10000.0f), 1000.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "proc_os", 1 }, "Processing oversampling",
                                                        StringArray { "1x", "2x", "4x", "8x" }, 0));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "src_cut", 1 }, "SRC cutoff", logRange (8000.0f, 40000.0f), 20000.0f,
                                                        AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "src_steep", 1 }, "SRC steepness",
                                                        StringArray { "Doux", "Moyen", "Raide", "Tres raide" }, 2));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "src_pre", 1 }, "SRC pre-ringing",
                                                        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                                        AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "True peak oversampling",
                                                        StringArray { "4x", "8x", "16x", "24x", "32x", "40x" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "look", 1 }, "Meter lookahead", true));
    // split compressor: one band (HP + LP edges with their own slopes), pre/post EQ, stereo or mid/side
    {
        StringArray cs { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "comp_on", 1 },   "Comp On", false));
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "comp_post", 1 }, "Comp after EQ", true));
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "comp_min", 1 },  "Comp minimum phase", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "comp_ms", 1 },   "Comp channels", StringArray { "Stereo", "Mid/Side", "Mid", "Side" }, 0));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "comp_lo", 1 },   "Comp low cut", logRange (20.0f, 20000.0f), 20.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "comp_hi", 1 },   "Comp high cut", logRange (20.0f, 20000.0f), 20000.0f));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "comp_slo", 1 },  "Comp low slope", cs, 2));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "comp_shi", 1 },  "Comp high slope", cs, 2));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "comp_thr", 1 },  "Comp threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "comp_ratio", 1 },"Comp ratio", logRange (1.0f, 20.0f), 2.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "comp_att", 1 },  "Comp attack", logRange (0.1f, 300.0f), 10.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "comp_rel", 1 },  "Comp release", logRange (10.0f, 3000.0f), 200.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "comp_knee", 1 }, "Comp knee", NormalisableRange<float> (0.0f, 18.0f, 0.1f), 6.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { "comp_mk", 1 },   "Comp makeup", NormalisableRange<float> (-6.0f, 18.0f, 0.1f), 0.0f));
    }
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "curves_on", 1 }, "Show EQ curves", true));
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
                                                            logRange (0.1f, 256.0f), 0.71f));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "ch"), 1 },    n + "Channel", chans, 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "slope"), 1 }, n + "Slope", slopes, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "fam"), 1 }, n + "Filter family",
                                                            StringArray { "Butterworth", "Linkwitz-Riley", "Critique", "Tchebychev 1 dB" }, 0));
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
    b.type = (eqd::BandType) juce::jlimit (0, 7, (int) std::lround (get ("type")));
    b.family = juce::jlimit (0, 3, (int) std::lround (get ("fam")));
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

eqd::Tilt MasteringEQAudioProcessor::readLine() const
{
    eqd::Tilt t;
    t.straight = true;
    t.on = apvts.getRawParameterValue ("tilt_line")->load() > 0.5f;
    t.db = apvts.getRawParameterValue ("line_db")->load();
    t.pivot = apvts.getRawParameterValue ("line_freq")->load();
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
    if (id == "out" || id == "bypass" || id == "tuning" || id == "theme" || id == "look" || id == "os"
        || id == "curves_on" || id == "curves_top") return;                          // no new filter needed
    if (id == "tilt_line") { designThread.trigger (4); return; }
    if (id == "line_db" || id == "line_freq") { designThread.trigger (4); return; }
    if (id == "comp_on") { updateLatency(); return; }
    if (id.startsWith ("comp_"))
    {
        if (id == "comp_lo" || id == "comp_hi" || id == "comp_slo" || id == "comp_shi" || id == "comp_min") designThread.trigger (2);
        return;                                                                       // dynamics are read per block
    }
    designThread.trigger (id == "quality" || id == "proc_os" || id == "src_cut" || id == "src_steep" || id == "src_pre" ? 7 : 5);
}

void MasteringEQAudioProcessor::updateLatency()
{
    const int K = std::max (1, activeK.load());
    const int work = latencyEq.load() + (apvts.getRawParameterValue ("comp_on")->load() > 0.5f ? latencyComp.load() : 0)
                     + (apvts.getRawParameterValue ("tilt_line")->load() > 0.5f ? latencyLine.load() : 0);
    latencyTarget = work / K + (K > 1 ? srcLatency.load() : 0);
    triggerAsyncUpdate();
}

// Straight-line tilt as its own last stage (after all bands and the split compressor)
void MasteringEQAudioProcessor::designLine (double fs, double fsw, int quality)
{
    const auto t = readLine();
    if (! t.on) { latencyLine = 0; updateLatency(); return; }
    eqd::Band none[eqd::kMaxBands];                         // all bands off: only the tilt shapes the response
    const auto r = eqd::design (none, eqd::kMaxBands, t, fsw, quality, readMode());
    auto load = [&] (HotConv& c, const std::vector<float>& v)
    {
        juce::AudioBuffer<float> b (1, (int) v.size());
        std::copy (v.begin(), v.end(), b.getWritePointer (0));
        c.loadImpulseResponse (std::move (b), fs, juce::dsp::Convolution::Stereo::no,
                               juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    };
    load (convTL, r.ll);
    load (convTR, r.rr);
    latencyLine = r.latencySamples;
    updateLatency();
}

void MasteringEQAudioProcessor::runDesign (int mask)
{
    const double fs = currentSampleRate.load();
    if (fs <= 0.0) return;

    const int K = 1 << juce::jlimit (0, 3, (int) std::lround (apvts.getRawParameterValue ("proc_os")->load()));
    const double fsw = fs * (double) K;                       // rate at which the filters are designed and run
    int quality = (int) std::lround (apvts.getRawParameterValue ("quality")->load());
    while (quality > 0 && eqd::irLengthFor (fsw, quality) > (1 << 20)) --quality;   // keep the FFT size bounded at high rates
    if (mask & 4) designLine (fs, fsw, quality);
    if (mask & 2)   // split-compressor band filter: same design engine and resolution as the EQ
    {
        static const int cs[4] { 6, 12, 24, 48 };
        auto get = [this] (const char* n) { return apvts.getRawParameterValue (n)->load(); };
        eqd::Band cb[2];
        cb[0].q = cb[1].q = 0.71;
        cb[0].type = eqd::BandType::HighPass; cb[0].freq = get ("comp_lo"); cb[0].slopeDbOct = cs[juce::jlimit (0, 3, (int) std::lround (get ("comp_slo")))];
        cb[0].on = cb[0].freq > 20.5;
        cb[1].type = eqd::BandType::LowPass;  cb[1].freq = get ("comp_hi"); cb[1].slopeDbOct = cs[juce::jlimit (0, 3, (int) std::lround (get ("comp_shi")))];
        cb[1].on = cb[1].freq < 19999.0;
        const auto cr = eqd::design (cb, 2, eqd::Tilt{}, fsw, quality, get ("comp_min") > 0.5f ? eqd::PhaseMode::Minimal : eqd::PhaseMode::Linear);
        for (auto* c : { &convCL, &convCR })
        {
            juce::AudioBuffer<float> b (1, (int) cr.ll.size());
            std::copy (cr.ll.begin(), cr.ll.end(), b.getWritePointer (0));
            c->loadImpulseResponse (std::move (b), fs, juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        }
        latencyComp = cr.latencySamples;
        if (! (mask & 1)) { updateLatency(); return; }
    }
    eqd::Band bands[eqd::kMaxBands];
    for (int i = 0; i < eqd::kMaxBands; ++i) bands[i] = readBand (i);
    const auto res = eqd::design (bands, eqd::kMaxBands, readTilt(), fsw, quality, readMode());

    auto load = [&] (HotConv& c, const std::vector<float>& v)
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
    latencyEq = res.latencySamples;
    {
        std::shared_ptr<SrcEngine> eng;
        if (K > 1)
        {
            const double nyq = fs * 0.5;
            const double cut = std::min ((double) apvts.getRawParameterValue ("src_cut")->load(), nyq * 0.999);
            eng = std::make_shared<SrcEngine> (K, fs, cut, (int) std::lround (apvts.getRawParameterValue ("src_steep")->load()),
                                               apvts.getRawParameterValue ("src_pre")->load() / 100.0, maxBlock);
        }
        srcLatency = eng ? eng->latency : 0;
        std::atomic_store (&srcEng, eng);
        srcRetired.push_back (eng);
        if (srcRetired.size() > 4) srcRetired.erase (srcRetired.begin());   // old engines are freed here, not on the audio thread
        activeK = K;
    }
    updateLatency();
}

void MasteringEQAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (latencyTarget.load());
}

void MasteringEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (samplesPerBlock, 64);
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock * 8, 1 };
    for (auto* c : { &convLL, &convLR, &convRL, &convRR }) { c->prepare (spec); c->reset(); }

    for (auto* b : { &bufLL, &bufLR, &bufRL, &bufRR }) b->setSize (1, maxBlock * 8, false, false, true);
    workIn.setSize (2, maxBlock * 8, false, false, true);
    dsBuf.setSize (2, maxBlock, false, false, true);
    dryBuf.setSize (2, maxBlock, false, false, true);
    tpScratch.setSize (2, maxBlock, false, false, true);
    inBuf.setSize (2, maxBlock, false, false, true);
    compBufL.setSize (1, maxBlock * 8, false, false, true);
    compBufR.setSize (1, maxBlock * 8, false, false, true);

    juce::dsp::ProcessSpec dspec { sampleRate, (juce::uint32) maxBlock, 2 };
    dryDelay.prepare (dspec);
    dryDelay.reset();
    compDelay.prepare (dspec);
    compDelay.reset();
    for (auto* c : { &convCL, &convCR, &convTL, &convTR }) { c->prepare (spec); c->reset(); }
    compState[0] = compState[1] = 0.0f;

    tpFactor = 0;
    for (auto& h : tpHist) h.assign ((size_t) kTpTaps, 0.0f);
    tpTmp.assign ((size_t) (maxBlock + kTpTaps), 0.0f);
    inLoud.prepare (sampleRate); outLoud.prepare (sampleRate);
    inRmsState = inPeakHold = 0.0f;
    vuCoef = 1.0f - std::exp (-1.0f / (0.065f * (float) sampleRate));
    ppmAttack = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    ppmDecay = std::pow (10.0f, -1.0f / (1.7f * (float) sampleRate));
    rmsCoef = 1.0f - std::exp (-1.0f / (0.3f * (float) sampleRate));
    for (int c = 0; c < 2; ++c) { vuState[c] = ppmState[c] = rmsState[c] = tpHold[c] = 0.0f; }

    gainSmooth.reset (sampleRate, 0.03);
    bypassSmooth.reset (sampleRate, 0.03);
    gainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out")->load()));
    bypassSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue ("bypass")->load() > 0.5f ? 1.0f : 0.0f);

    runDesign (7);   // synchronous first design so a filter is available immediately
}

bool MasteringEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void MasteringEQAudioProcessor::updateMeters (const juce::AudioBuffer<float>& buffer, int start, int n, int numCh)
{
    if (resetTruePeak.exchange (false))
    {
        tpHold[0] = tpHold[1] = 0.0f; inPeakHold = 0.0f;
        inLoud.requestReset(); outLoud.requestReset();
    }
    const bool look = apvts.getRawParameterValue ("look")->load() > 0.5f;
    static const int kOs[6] { 4, 8, 16, 24, 32, 40 };
    const int osK = kOs[juce::jlimit (0, 5, (int) std::lround (apvts.getRawParameterValue ("os")->load()))];
    const float lookGain = gainSmooth.getTargetValue();

    // loudness (K-weighted) of input and output
    {
        const float* ip[2] { inBuf.getReadPointer (0), inBuf.getReadPointer (numCh - 1) };
        inLoud.process (ip, numCh, n);
        const float* op[2] { buffer.getReadPointer (0) + start, buffer.getReadPointer (numCh - 1) + start };
        outLoud.process (op, numCh, n);
        float r2 = inRmsState, pk = inPeakHold;
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float v = ip[ch][i];
                r2 += (v * v - r2) * rmsCoef / (float) numCh;
                pk = std::max (pk, std::abs (v));
            }
        inRmsState = r2; inPeakHold = pk;
        inRmsDb.store (10.0f * std::log10 (std::max (r2, 1.0e-12f)));
        inPeakDb.store (juce::Decibels::gainToDecibels (pk, -120.0f));
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        // needles: from the output (what is heard) or, with lookahead, from the input that arrives one latency earlier
        const float* x = look ? inBuf.getReadPointer (ch) : buffer.getReadPointer (ch) + start;
        const float sc = look ? lookGain : 1.0f;
        const float* xo = buffer.getReadPointer (ch) + start;
        float vu = vuState[ch], ppm = ppmState[ch], r2 = rmsState[ch];
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (x[i]) * sc;
            vu += (a - vu) * vuCoef;
            if (a > ppm) ppm += (a - ppm) * ppmAttack; else ppm *= ppmDecay;
            r2 += (xo[i] * xo[i] - r2) * rmsCoef;
        }
        vuState[ch] = vu; ppmState[ch] = ppm; rmsState[ch] = r2;
        meters[ch].vu.store (juce::Decibels::gainToDecibels (vu * 1.1107f, -120.0f));
        meters[ch].ppm.store (juce::Decibels::gainToDecibels (ppm, -120.0f));
        meters[ch].rms.store (10.0f * std::log10 (std::max (r2, 1.0e-12f)));
    }

    // true peak of the output, interpolated by osK (polyphase windowed sinc)
    if (osK != tpFactor) buildTruePeakKernel (osK);
    const int T = kTpTaps;
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* tmp = tpTmp.data();
        std::copy (tpHist[ch].begin(), tpHist[ch].end(), tmp);
        std::copy (buffer.getReadPointer (ch) + start, buffer.getReadPointer (ch) + start + n, tmp + (T - 1));
        float m = tpHold[ch];
        for (int i = 0; i < n; ++i)
        {
            const float* x = tmp + (T - 1) + i;                 // x[-k] = x[0 - k]
            m = std::max (m, std::abs (x[-(T / 2)]));
            for (int p = 1; p < osK; ++p)
            {
                const float* c = tpCoef.data() + (size_t) (p - 1) * (size_t) T;
                float acc = 0.0f;
                for (int k = 0; k < T; ++k) acc += c[k] * x[-k];
                m = std::max (m, std::abs (acc));
            }
        }
        std::copy (tmp + n, tmp + n + (T - 1), tpHist[ch].begin());
        tpHold[ch] = m;
        meters[ch].truePeak.store (juce::Decibels::gainToDecibels (m, -120.0f));
    }
}

void MasteringEQAudioProcessor::buildTruePeakKernel (int K)
{
    const int T = kTpTaps;
    tpFactor = K;
    tpCoef.assign ((size_t) (K - 1) * (size_t) T, 0.0f);
    auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
    const double beta = 9.0, pi = 3.14159265358979323846, I0b = i0 (beta);
    for (int p = 1; p < K; ++p)
        for (int k = 0; k < T; ++k)
        {
            const double d = (double) k - (double) T / 2.0 + (double) p / (double) K;   // distance from the interpolation point
            const double r = d / ((double) T / 2.0);
            const double w = std::abs (r) < 1.0 ? i0 (beta * std::sqrt (1.0 - r * r)) / I0b : 0.0;
            const double sinc = std::abs (d) < 1e-9 ? 1.0 : std::sin (pi * d) / (pi * d);
            tpCoef[(size_t) (p - 1) * (size_t) T + (size_t) k] = (float) (sinc * w);
        }
}

// Split compressor: the signal is cut into one band (linear or minimum phase FIR, same engine as the EQ),
// only that band is compressed and added back to the untouched rest (rest = delayed input - band).
void MasteringEQAudioProcessor::runComp (float* L, float* R, int n)
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const double fs = std::max (currentSampleRate.load(), 8000.0) * (double) curK;
    const float thr = get ("comp_thr"), ratio = get ("comp_ratio"), knee = get ("comp_knee"), mk = get ("comp_mk");
    const float aC = std::exp (-1.0f / (std::max (get ("comp_att"), 0.05f) * 0.001f * (float) fs));
    const float rC = std::exp (-1.0f / (std::max (get ("comp_rel"), 1.0f) * 0.001f * (float) fs));
    const int ms = (int) std::lround (get ("comp_ms"));
    const float slope = 1.0f / std::max (ratio, 1.0f) - 1.0f;

    auto conv = [n] (HotConv& c, juce::AudioBuffer<float>& b)
    {
        juce::dsp::AudioBlock<float> blk (b);
        auto sub = blk.getSubBlock (0, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        c.process (ctx);
    };
    std::copy (L, L + n, compBufL.getWritePointer (0));
    conv (convCL, compBufL);
    if (R) { std::copy (R, R + n, compBufR.getWritePointer (0)); conv (convCR, compBufR); }

    compDelay.setDelay ((float) std::min (latencyComp.load(), 1090000));
    auto gain = [&] (float det, float& st)
    {
        const float lvl = 20.0f * std::log10 (std::max (det, 1.0e-6f));
        const float over = lvl - thr;
        float gr;
        if (2.0f * over < -knee) gr = 0.0f;
        else if (knee > 0.0f && 2.0f * std::abs (over) <= knee) { const float t = over + knee * 0.5f; gr = slope * t * t / (2.0f * knee); }
        else gr = slope * over;
        const float c = gr < st ? aC : rC;
        st = c * st + (1.0f - c) * gr;
        return std::pow (10.0f, (st + mk) / 20.0f);
    };

    const float* bl = compBufL.getReadPointer (0);
    const float* br = R ? compBufR.getReadPointer (0) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        compDelay.pushSample (0, L[i]);
        const float dl = compDelay.popSample (0);
        float dr = 0.0f;
        if (R) { compDelay.pushSample (1, R[i]); dr = compDelay.popSample (1); }

        if (! R || ms == 0)
        {
            const float g = gain (R ? std::max (std::abs (bl[i]), std::abs (br[i])) : std::abs (bl[i]), compState[0]);
            L[i] = dl + (g - 1.0f) * bl[i];
            if (R) R[i] = dr + (g - 1.0f) * br[i];
        }
        else
        {
            const float m = 0.5f * (bl[i] + br[i]), s = 0.5f * (bl[i] - br[i]);
            const float gm = (ms == 1 || ms == 2) ? gain (std::abs (m), compState[0]) : 1.0f;
            const float gs = (ms == 1 || ms == 3) ? gain (std::abs (s), compState[1]) : 1.0f;
            const float m2 = m * gm, s2 = s * gs;
            L[i] = dl + (m2 + s2 - bl[i]);
            R[i] = dr + (m2 - s2 - br[i]);
        }
    }
    compGrDb = -std::min (compState[0], (R && ms != 0) ? compState[1] : 0.0f);
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
    auto eng = std::atomic_load (&srcEng);
    const int K = eng ? std::max (1, eng->K) : 1;
    curK = K;

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
                inBuf.getWritePointer (ch)[i] = in[i];
                dryDelay.pushSample (ch, in[i]);
                d[i] = dryDelay.popSample (ch);
            }
        }

        // whole chain (split comp, EQ, straight tilt) at K x the host rate
        const float *wetL, *wetR;
        if (K == 1)
        {
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (std::min (ch, numCh - 1)) + start, buffer.getReadPointer (std::min (ch, numCh - 1)) + start + n, workIn.getWritePointer (ch));
            chain (workIn.getWritePointer (0), numCh == 2 ? workIn.getWritePointer (1) : nullptr, n, cross);
            wetL = bufLL.getReadPointer (0);
            wetR = bufRR.getReadPointer (0);
        }
        else
        {
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (std::min (ch, numCh - 1)) + start, buffer.getReadPointer (std::min (ch, numCh - 1)) + start + n, workIn.getWritePointer (ch));
            const float* inp[2] { workIn.getReadPointer (0), workIn.getReadPointer (1) };
            eng->up (inp, n);
            const int nn = n * K;
            chain (eng->upChannel (0), numCh == 2 ? eng->upChannel (1) : nullptr, nn, cross);
            std::copy (bufLL.getReadPointer (0), bufLL.getReadPointer (0) + nn, eng->upChannel (0));
            if (numCh == 2) std::copy (bufRR.getReadPointer (0), bufRR.getReadPointer (0) + nn, eng->upChannel (1));
            float* outp[2] { dsBuf.getWritePointer (0), dsBuf.getWritePointer (1) };
            eng->down (outp, n);
            wetL = dsBuf.getReadPointer (0);
            wetR = dsBuf.getReadPointer (1);
        }

        for (int i = 0; i < n; ++i)
        {
            const float g = gainSmooth.getNextValue();
            const float bp = bypassSmooth.getNextValue();
            float mono = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float wet = (ch == 0 ? wetL[i] : wetR[i]) * g;
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

// Split comp (pre) -> EQ -> split comp (post) -> straight tilt, all at the working rate; result in bufLL / bufRR
void MasteringEQAudioProcessor::chain (float* xL, float* xR, int nn, bool cross)
{
    auto run = [nn] (HotConv& c, juce::AudioBuffer<float>& b)
    {
        juce::dsp::AudioBlock<float> blk (b);
        auto sub = blk.getSubBlock (0, (size_t) nn);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        c.process (ctx);
    };
    const bool compOn = apvts.getRawParameterValue ("comp_on")->load() > 0.5f;
    const bool compPost = apvts.getRawParameterValue ("comp_post")->load() > 0.5f;
    if (! compOn) compGrDb = compGrDb.load() * 0.9f;
    if (compOn && ! compPost) runComp (xL, xR, nn);

    std::copy (xL, xL + nn, bufLL.getWritePointer (0));
    run (convLL, bufLL);
    if (xR != nullptr)
    {
        std::copy (xR, xR + nn, bufRR.getWritePointer (0));
        run (convRR, bufRR);
        if (cross)
        {
            std::copy (xR, xR + nn, bufLR.getWritePointer (0));   // R -> L
            std::copy (xL, xL + nn, bufRL.getWritePointer (0));   // L -> R
            run (convLR, bufLR);
            run (convRL, bufRL);
            bufLL.addFrom (0, 0, bufLR, 0, 0, nn);
            bufRR.addFrom (0, 0, bufRL, 0, 0, nn);
        }
    }
    if (compOn && compPost) runComp (bufLL.getWritePointer (0), xR != nullptr ? bufRR.getWritePointer (0) : nullptr, nn);
    if (apvts.getRawParameterValue ("tilt_line")->load() > 0.5f)
    {
        run (convTL, bufLL);
        if (xR != nullptr) run (convTR, bufRR);
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
