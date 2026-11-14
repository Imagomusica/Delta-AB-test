#include <cstring>
#include <cstdlib>
#include "PluginEditor.h"

static double compBandGain (juce::AudioProcessorValueTreeState& a, double f);

namespace
{
constexpr float kW = 1240.0f, kH = 790.0f;

juce::String fmtFreq (double f)
{
    if (f < 100.0)   return juce::String (f, 2) + " Hz";
    if (f < 1000.0)  return juce::String (f, 1) + " Hz";
    if (f < 10000.0) return juce::String (f / 1000.0, 2) + " kHz";
    return juce::String (f / 1000.0, 1) + " kHz";
}

juce::String noteName (double f, double ref)
{
    static const juce::String names[12] = { "Do", "Do#", juce::String::fromUTF8 ("R\xC3\xA9"), juce::String::fromUTF8 ("R\xC3\xA9#"),
                                            "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    const double m = 69.0 + 12.0 * std::log2 (std::max (f, 1.0) / ref);
    const int mi = (int) std::lround (m);
    const int pc = ((mi % 12) + 12) % 12;
    const int oct = (int) std::floor ((double) mi / 12.0) - 2;
    return names[pc] + juce::String (oct);
}

void fillCombo (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, juce::ComboBox& box)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        box.addItemList (c->choices, 1);
}

void setParamNormalised (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float actual)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (actual));
}

float paramValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    return apvts.getRawParameterValue (id)->load();
}

float lookup (const std::array<float, 512>& a, float u)
{
    const float x = juce::jlimit (0.0f, 1.0f, u) * 511.0f;
    const int i = (int) x;
    const int j = std::min (i + 1, 511);
    return a[(size_t) i] + (a[(size_t) j] - a[(size_t) i]) * (x - (float) i);
}
} // namespace

Palette paletteFor (int theme)
{
    switch (theme)
    {
        case 0:  return { juce::Colour (0xfff4f4f0), juce::Colour (0xff121212), juce::Colour (0xff8c8c88), juce::Colour (0xffcdcdc8) };  // White 9016
        case 1:  return { juce::Colour (0xff2b3033), juce::Colour (0xfff1f1ee), juce::Colour (0xff90979a), juce::Colour (0xff4b5357) };  // Anthracite 7016
        default: return { juce::Colour (0xff000000), juce::Colour (0xffffffff), juce::Colour (0xff8a8a8a), juce::Colour (0xff3a3a3a) };  // Black 9005
    }
}

// ===========================================================================
// Look and feel
juce::Font DuneLnF::font (float size, int style) const
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", size, style));
}

void DuneLnF::setPalette (const Palette& p)
{
    pal = p;
    using C = juce::ComboBox;
    setColour (C::backgroundColourId, p.bg);
    setColour (C::outlineColourId, p.fg.withAlpha (0.85f));
    setColour (C::textColourId, p.fg);
    setColour (C::arrowColourId, p.fg);
    setColour (C::buttonColourId, p.bg);
    setColour (juce::PopupMenu::backgroundColourId, p.bg);
    setColour (juce::PopupMenu::textColourId, p.fg);
    setColour (juce::PopupMenu::headerTextColourId, p.dim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.fg);
    setColour (juce::PopupMenu::highlightedTextColourId, p.bg);
    setColour (juce::TextButton::buttonColourId, p.bg);
    setColour (juce::TextButton::buttonOnColourId, p.fg);
    setColour (juce::TextButton::textColourOffId, p.fg);
    setColour (juce::TextButton::textColourOnId, p.bg);
    setColour (juce::Label::textColourId, p.fg);
    setColour (juce::Slider::thumbColourId, p.fg);
}

juce::Font DuneLnF::getComboBoxFont (juce::ComboBox&) { return font (12.5f); }
juce::Font DuneLnF::getPopupMenuFont() { return font (13.0f); }
juce::Font DuneLnF::getTextButtonFont (juce::TextButton&, int) { return font (11.5f, juce::Font::bold); }

void DuneLnF::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto b = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f);
    g.setColour (pal.bg);
    g.fillRect (b);
    g.setColour (pal.fg.withAlpha (box.isEnabled() ? 0.85f : 0.4f));
    g.drawRect (b, 1.0f);
    juce::Path tri;
    const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
    tri.addTriangle (cx - 4.5f, cy - 2.5f, cx + 4.5f, cy - 2.5f, cx, cy + 3.0f);
    g.setColour (pal.fg);
    g.fillPath (tri);
}

void DuneLnF::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void DuneLnF::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    auto b = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    g.setColour (on ? pal.fg : pal.bg);
    g.fillRect (b);
    if (! on && (highlighted || down))
    {
        g.setColour (pal.fg.withAlpha (0.14f));
        g.fillRect (b);
    }
    g.setColour (pal.fg.withAlpha (0.85f));
    g.drawRect (b, 1.0f);
}

void DuneLnF::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.getToggleState() ? pal.bg : pal.fg);
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

void DuneLnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                                juce::Slider& slider)
{
    // Nagra style: scale ticks on the panel, recessed round bowl, flat tapered lever
    const float r = (float) juce::jmin (w, h) * 0.5f - 1.0f;
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
    const float a = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) slider.getProperties()["bipolar"];

    const int nT = 11;
    for (int i = 0; i < nT; ++i)
    {
        const float t = (float) i / (float) (nT - 1), ang = startAngle + t * (endAngle - startAngle);
        const bool major = (i == 0 || i == nT - 1 || (bipolar && i == nT / 2) || i == nT / 2);
        const float r0 = r * (major ? 0.84f : 0.89f);
        g.setColour (major ? pal.fg : pal.dim);
        g.drawLine (cx + std::sin (ang) * r0, cy - std::cos (ang) * r0, cx + std::sin (ang) * r, cy - std::cos (ang) * r, major ? 1.6f : 1.0f);
    }

    const float rb = r * 0.76f;
    g.setGradientFill (juce::ColourGradient (pal.fg.withAlpha (0.85f), cx - rb, cy - rb, pal.fg.withAlpha (0.18f), cx + rb, cy + rb, false));
    g.fillEllipse (cx - rb, cy - rb, rb * 2.0f, rb * 2.0f);                       // metal rim
    const float ri = rb * 0.86f;
    g.setGradientFill (juce::ColourGradient (pal.bg.withAlpha (1.0f), cx - ri, cy - ri, pal.fg.withAlpha (0.16f), cx + ri, cy + ri, false));
    g.fillEllipse (cx - ri, cy - ri, ri * 2.0f, ri * 2.0f);                        // recess
    g.setColour (pal.bg.withAlpha (0.9f));
    g.drawEllipse (cx - ri, cy - ri, ri * 2.0f, ri * 2.0f, 1.0f);

    // lever: tapered flat bar, wide at the hub, narrow at the tip
    const float tip = ri * 0.98f, tail = ri * 0.30f, wHub = ri * 0.34f, wTip = ri * 0.12f;
    juce::Path lever;
    lever.startNewSubPath (tip, -wTip);
    lever.lineTo (tip, wTip);
    lever.lineTo (-tail, wHub);
    lever.lineTo (-tail, -wHub);
    lever.closeSubPath();
    const auto tr = juce::AffineTransform::rotation (a - juce::MathConstants<float>::halfPi).translated (cx, cy);
    lever.applyTransform (tr);
    g.setGradientFill (juce::ColourGradient (pal.fg, cx - rb, cy - rb, pal.fg.withAlpha (0.55f), cx + rb, cy + rb, false));
    g.fillPath (lever);
    g.setColour (pal.bg.withAlpha (0.7f));
    g.strokePath (lever, juce::PathStrokeType (0.9f));
    g.setColour (pal.fg);
    g.drawLine (cx + std::sin (a) * ri * 0.15f, cy - std::cos (a) * ri * 0.15f, cx + std::sin (a) * ri * 0.92f, cy - std::cos (a) * ri * 0.92f, 0.8f);
    g.setColour (pal.bg);
    g.fillEllipse (cx - ri * 0.1f, cy - ri * 0.1f, ri * 0.2f, ri * 0.2f);            // hub screw
}

void DuneLnF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                                juce::Slider::SliderStyle, juce::Slider&)
{
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (pal.ring);
    g.fillRect ((float) x, cy - 0.5f, (float) w, 1.0f);
    g.setColour (pal.fg);
    g.fillRect (sliderPos - 1.5f, cy - 8.0f, 3.0f, 16.0f);
}

// ===========================================================================
// Dune field + modulometer
DuneField::DuneField (MasteringEQAudioProcessor& p, const Palette& pl) : proc (p), pal (pl)
{
    juce::Random rng (20261112);
    auto gauss = [&] { return (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 2.0f) * 1.73f; };
    auto add = [&] (int layer, int count)
    {
        for (int i = 0; i < count; ++i) particles.push_back ({ rng.nextFloat(), gauss(), rng.nextFloat(), layer });
    };
    for (int l = 0; l < kLayers; ++l) add (l, 1500);
    add (kLayers, 3400);       // curve L / stereo
    add (kLayers + 1, 1500);   // curve M
    add (kLayers + 2, 1500);   // curve S
    spectrum.fill (-120.0f);
    demo = std::getenv ("DUNE_DEMO") != nullptr;
    meterPanel = { 690.0f, 266.0f, 546.0f, 148.0f };
    startTimerHz (30);
}

void DuneField::mouseDown (const juce::MouseEvent& e)
{
    if (meterPanel.contains (e.position)) proc.resetTruePeak = true;
}

void DuneField::updateAnalyzer()
{
    const int pos = proc.anaPos.load (std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = proc.anaBuf[(size_t) ((pos - fftSize + i + 4 * MasteringEQAudioProcessor::anaSize)
                                                    % MasteringEQAudioProcessor::anaSize)];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    for (int k = 0; k < fftSize / 2; ++k)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) k] / (0.25f * (float) fftSize), -120.0f);
        spectrum[(size_t) k] = 0.75f * spectrum[(size_t) k] + 0.25f * db;
    }
}

void DuneField::timerCallback()
{
    if (demo)   // synthetic stereo music-like signal, only used for screenshots (DUNE_DEMO=1)
    {
        static int ph = 0;
        for (int i = 0; i < 1024; ++i, ++ph)
        {
            const float s = (float) ph;
            const float a = 0.22f * std::sin (s * 0.021f) + 0.12f * std::sin (s * 0.19f) + 0.06f * std::sin (s * 0.73f)
                            + 0.04f * std::sin (s * 1.9f);
            const float b = 0.20f * std::sin (s * 0.021f + 0.5f) + 0.10f * std::sin (s * 0.23f) + 0.05f * std::sin (s * 0.61f);
            const int pos = proc.anaPos.load();
            proc.anaL[(size_t) pos] = a; proc.anaR[(size_t) pos] = b; proc.anaBuf[(size_t) pos] = 0.5f * (a + b);
            proc.anaPos.store ((pos + 1) % MasteringEQAudioProcessor::anaSize);
        }
        proc.meters[0].rms = -21.0f; proc.meters[1].rms = -22.0f; proc.meters[0].vu = -19.0f; proc.meters[1].vu = -20.0f;
        proc.inLoud.momentary = -17.0f; proc.inLoud.shortTerm = -18.2f; proc.inLoud.integrated = -18.6f;
        proc.outLoud.momentary = -16.1f; proc.outLoud.shortTerm = -17.4f; proc.outLoud.integrated = -17.8f;
        proc.inRmsDb = -22.0f; proc.inPeakDb = -4.2f;
        proc.meters[0].ppm = -9.0f; proc.meters[1].ppm = -10.0f; proc.meters[0].truePeak = -6.0f; proc.meters[1].truePeak = -6.5f;
    }

    // level and transient energy drive the dunes
    const float rmsDb = 0.5f * (proc.meters[0].rms.load() + proc.meters[1].rms.load());
    const float ppmDb = 0.5f * (proc.meters[0].ppm.load() + proc.meters[1].ppm.load());
    const float lvTarget = juce::jlimit (0.0f, 1.0f, (rmsDb + 60.0f) / 48.0f);
    const float trTarget = juce::jlimit (0.0f, 1.0f, (ppmDb - rmsDb - 6.0f) / 12.0f);
    level += (lvTarget - level) * (lvTarget > level ? 0.35f : 0.08f);
    transient += (trTarget - transient) * (trTarget > transient ? 0.5f : 0.12f);
    t += 0.02f + 0.07f * level;
    updateAnalyzer();

    // goniometer points + correlation from the last samples
    {
        const int pos = proc.anaPos.load (std::memory_order_acquire);
        const int n = 4096, shown = 700;
        double sLR = 0.0, sLL = 0.0, sRR = 0.0;
        float pk = 0.02f;
        gonio.clear();
        for (int i = 0; i < n; ++i)
        {
            const int idx = (pos - n + i + 4 * MasteringEQAudioProcessor::anaSize) % MasteringEQAudioProcessor::anaSize;
            const float l = proc.anaL[(size_t) idx], r = proc.anaR[(size_t) idx];
            sLR += (double) l * r; sLL += (double) l * l; sRR += (double) r * r;
            pk = std::max (pk, std::max (std::abs (l), std::abs (r)));
            if (i >= n - shown) gonio.push_back ({ (l - r) * 0.70711f, (l + r) * 0.70711f });
        }
        corr = (sLL > 1e-12 && sRR > 1e-12) ? (float) (sLR / std::sqrt (sLL * sRR)) : 0.0f;
        gonioPeak += (pk - gonioPeak) * 0.2f;
    }

    eqd::Band bands[eqd::kMaxBands];
    anyMS = false;
    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        bands[i] = proc.readBand (i);
        if (bands[i].on && (bands[i].ch == eqd::Channel::Mid || bands[i].ch == eqd::Channel::Side)) anyMS = true;
    }
    const auto tilt = proc.readTilt();
    const auto line = proc.readLine();
    const double fs = std::max (proc.getSampleRate(), 8000.0);
    for (int i = 0; i < kPoints; ++i)
    {
        const double u = (double) i / (double) (kPoints - 1);
        const double f = 3.0 * std::pow (10000.0, u);
        const double lineDb = eqd::tiltDb (line, f);
        dbL[(size_t) i] = (float) (eqd::totalMagnitudeDb (bands, eqd::kMaxBands, eqd::Channel::Left, f, &tilt) + lineDb);
        dbM[(size_t) i] = (float) eqd::totalMagnitudeDb (bands, eqd::kMaxBands, eqd::Channel::Mid, f, nullptr) + dbL[(size_t) i];
        dbS[(size_t) i] = (float) eqd::totalMagnitudeDb (bands, eqd::kMaxBands, eqd::Channel::Side, f, nullptr) + dbL[(size_t) i];
        const int bin = juce::jlimit (1, fftSize / 2 - 1, (int) std::lround (f / fs * (double) fftSize));
        specN[(size_t) i] = juce::jlimit (0.0f, 1.0f, (spectrum[(size_t) bin] + 90.0f) / 70.0f);
    }

    for (int i = 0; i < kPoints; ++i)
        compMag[(size_t) i] = (float) compBandGain (proc.apvts, 3.0 * std::pow (10000.0, (double) i / (double) (kPoints - 1)));

    // phase response of the EQ (analog-equivalent phase, unwrapped); linear mode has none
    if (proc.readMode() == eqd::PhaseMode::Linear)
    {
        phaseDeg.fill (0.0f);
    }
    else
    {
        double prev = 0.0, off = 0.0;
        for (int i = 0; i < kPoints; ++i)
        {
            const double f = 3.0 * std::pow (10000.0, (double) i / (double) (kPoints - 1));
            eqd::cd A (1.0, 0.0);
            for (int b = 0; b < eqd::kMaxBands; ++b)
                if (bands[b].on && eqd::bandHitsGroup (bands[b], eqd::Channel::Left)) A *= eqd::bandResponse (bands[b], f);
            double ph = std::arg (A);
            if (i > 0)
            {
                while (ph + off - prev > eqd::kPi) off -= 2.0 * eqd::kPi;
                while (ph + off - prev < -eqd::kPi) off += 2.0 * eqd::kPi;
            }
            prev = ph + off;
            phaseDeg[(size_t) i] = (float) (prev * 180.0 / eqd::kPi);
        }
    }
    repaint();
}

static double compBandGain (juce::AudioProcessorValueTreeState& a, double f)
{
    static const int cs[4] { 6, 12, 24, 48 };
    auto get = [&] (const char* id) { return (double) a.getRawParameterValue (id)->load(); };
    double g = 1.0;
    const double lo = get ("comp_lo"), hi = get ("comp_hi");
    if (lo > 20.5)
    {
        eqd::Band b; b.on = true; b.type = eqd::BandType::HighPass; b.freq = lo; b.q = 0.71;
        b.slopeDbOct = cs[juce::jlimit (0, 3, (int) std::lround (get ("comp_slo")))];
        g *= std::abs (eqd::bandResponse (b, f));
    }
    if (hi < 19999.0)
    {
        eqd::Band b; b.on = true; b.type = eqd::BandType::LowPass; b.freq = hi; b.q = 0.71;
        b.slopeDbOct = cs[juce::jlimit (0, 3, (int) std::lround (get ("comp_shi")))];
        g *= std::abs (eqd::bandResponse (b, f));
    }
    return g;
}

static void setParamValue (juce::AudioProcessorValueTreeState& a, const char* id, float v)
{
    if (auto* p = a.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}

namespace { const char* kOsNames[] { "4x", "8x", "16x", "24x", "32x", "40x" }; }

void NagraSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getHeight(), 26.0f);
    const juce::Point<float> c (b.getX() + d * 0.5f + 1.0f, b.getCentreY());
    const bool on = getToggleState();

    // recessed bezel
    g.setGradientFill (juce::ColourGradient (pal.fg.withAlpha (0.55f), c.x, c.y - d * 0.5f, pal.fg.withAlpha (0.12f), c.x, c.y + d * 0.5f, false));
    g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
    g.setColour (pal.bg);
    g.fillEllipse (c.x - d * 0.5f + 2.5f, c.y - d * 0.5f + 2.5f, d - 5.0f, d - 5.0f);
    g.setColour (pal.ring);
    g.drawEllipse (c.x - d * 0.5f + 2.5f, c.y - d * 0.5f + 2.5f, d - 5.0f, d - 5.0f, 1.0f);

    // lever (brushed metal), up = ON, down = OFF
    const float len = d * 0.40f, w = 6.0f;
    const float ty = on ? c.y - len : c.y + len;
    juce::Path lever;
    lever.addRoundedRectangle (c.x - w * 0.5f, std::min (c.y, ty) - 1.0f, w, len + 2.0f, 2.5f);
    g.setGradientFill (juce::ColourGradient (pal.fg.withAlpha (0.35f), c.x - w * 0.5f, c.y, pal.fg, c.x + w * 0.5f, c.y, false));
    g.fillPath (lever);
    g.setColour (pal.fg);
    g.fillEllipse (c.x - 4.0f, ty - 4.0f, 8.0f, 8.0f);                       // lever tip
    g.setColour (pal.bg.withAlpha (0.5f));
    g.fillEllipse (c.x - 1.5f, ty - 2.2f, 2.2f, 2.2f);                        // highlight

    g.setColour (on ? pal.fg : pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 11.0f, on ? juce::Font::bold : juce::Font::plain)));
    g.drawText (label.isEmpty() ? getButtonText() : label, (int) (c.x + d * 0.5f + 6.0f), (int) b.getY(),
                (int) (b.getRight() - c.x - d * 0.5f - 6.0f), (int) b.getHeight(), juce::Justification::centredLeft);
    if (highlighted)
    {
        g.setColour (pal.fg.withAlpha (0.25f));
        g.drawEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.0f);
    }
}

void DuneField::paint (juce::Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float stripH = 120.0f, Hm = H - stripH;
    g.fillAll (pal.bg);

    const bool showCurves = proc.apvts.getRawParameterValue ("curves_on")->load() > 0.5f || std::getenv ("DUNE_CURVES") != nullptr;
    const bool curvesTop  = proc.apvts.getRawParameterValue ("curves_top")->load() > 0.5f;

    juce::RectangleList<float> bright, mid, faint, curveM, curveS, compEdge, compFill;
    const bool compOn = proc.apvts.getRawParameterValue ("comp_on")->load() > 0.5f;
    const float dot = 1.35f;
    const float specGain = 0.10f + 0.22f * level;          // the dunes move with the music
    const float jitterGain = 1.0f + 1.5f * transient;
    for (const auto& p : particles)
    {
        float y;
        if (p.layer < kLayers)
        {
            const float b = ((float) p.layer + 0.5f) / (float) kLayers;
            const float l = (float) p.layer;
            if (std::sin (p.u * 11.0f + l * 2.3f + t * 0.15f) + p.h * 1.5f < 0.05f) continue;
            const float n = std::sin (p.u * 6.0f + l * 1.7f + t * 0.35f) + 0.6f * std::sin (p.u * 15.0f + l * 2.9f - t * 0.5f)
                            + 0.3f * std::sin (p.u * 37.0f + l * 0.9f + t * 0.9f);
            const float slope = ((p.layer & 1) ? 1.0f : -1.0f) * (p.u - 0.5f) * 0.30f + 0.08f * std::sin (l * 1.3f);
            const float wl = 0.35f + 0.65f * (1.0f - std::abs (2.0f * b - 1.0f));
            const float resp = -lookup (dbL, p.u) / 18.0f * 0.22f * wl;
            const float spec = -lookup (specN, p.u) * specGain;
            y = Hm * (b + 0.06f * n * (1.0f + 0.6f * level) + slope + resp + spec) + p.jy * (2.5f + 6.0f * p.h) * jitterGain;
            if (y < 0.0f || y > Hm) continue;
            juce::Rectangle<float> r (p.u * W, y, dot, dot);
            if (p.h > 0.88f) mid.addWithoutMerging (r); else faint.addWithoutMerging (r);
        }
        else if (showCurves)
        {
            const int c = p.layer - kLayers;
            if (c > 0 && ! anyMS) continue;
            const auto& arr = c == 0 ? dbL : (c == 1 ? dbM : dbS);
            const float vib = 0.75f + 0.5f * std::sin (t * 4.3f + p.u * 91.0f + p.h * 37.0f);          // the grains tremble a little
            const float wob = 0.9f * std::sin (t * 2.7f + p.u * 43.0f) * (1.0f + 2.0f * transient);
            y = Hm * 0.5f - lookup (arr, p.u) / 18.0f * Hm * 0.42f + p.jy * (c == 0 ? 2.2f : 1.6f) * vib + wob;
            if (c == 0 && compOn)
            {
                const float yb = Hm * 0.94f, top = yb - Hm * 0.30f * lookup (compMag, p.u);
                const float yEdge = top + p.jy * 1.6f * vib + 0.8f * std::sin (t * 3.1f + p.u * 57.0f);
                if (yEdge > 0.0f && yEdge < Hm) compEdge.addWithoutMerging ({ p.u * W, yEdge, dot * 1.2f, dot * 1.2f });
                const float yf = yb - (yb - top) * p.h;
                if (p.h > 0.30f && yb - top > 2.0f) compFill.addWithoutMerging ({ p.u * W, yf + 0.6f * std::sin (t * 2.2f + p.h * 50.0f), dot, dot });
            }
            if (y < 0.0f || y > Hm) continue;
            juce::Rectangle<float> r (p.u * W, y, dot * 1.15f, dot * 1.15f);
            if (c == 0) bright.addWithoutMerging (r); else if (c == 1) curveM.addWithoutMerging (r); else curveS.addWithoutMerging (r);
        }
    }
    auto drawCurves = [&]
    {
        g.setColour (pal.fg.withAlpha (0.8f));
        g.fillRectList (curveM);
        g.fillRectList (curveS);
        g.setColour (pal.fg);
        g.fillRectList (bright);
    };
    if (showCurves && ! curvesTop) drawCurves();
    g.setColour (pal.fg.withAlpha (0.28f + 0.30f * level));
    g.fillRectList (faint);
    g.setColour (pal.fg.withAlpha (0.60f + 0.25f * level));
    g.fillRectList (mid);
    if (showCurves && curvesTop) drawCurves();
    if (compOn)
    {
        g.setColour (pal.fg.withAlpha (0.38f));
        g.fillRectList (compFill);
        g.setColour (pal.fg);
        g.fillRectList (compEdge);
    }

    // frequency / dB hints
    g.setColour (pal.dim.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 10.0f, juce::Font::plain)));
    for (double f : { 3.0, 10.0, 100.0, 1000.0, 10000.0 })
    {
        const float x = (float) (std::log (f / 3.0) / std::log (10000.0)) * W;
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                    juce::jlimit (0, (int) W - 32, (int) x - 16), (int) Hm - 15, 32, 12, juce::Justification::centred);
    }
    if (showCurves)
    {
        g.drawText ("+18 dB", 620, (int) (Hm * 0.08f) - 6, 50, 12, juce::Justification::right);
        g.drawText ("0 dB", 620, (int) (Hm * 0.5f) - 6, 50, 12, juce::Justification::right);
        g.drawText ("-18 dB", 620, (int) (Hm * 0.92f) - 6, 50, 12, juce::Justification::right);
    }

    // ---- phase strip (oscilloscope style): EQ phase response + stereo goniometer
    g.setColour (pal.ring);
    const float zoneW = 680.0f;
    g.drawHorizontalLine ((int) Hm, 0.0f, zoneW);
    const float cy = Hm + stripH * 0.5f, ph = stripH * 0.5f - 14.0f;
    const float gonioW = 170.0f, plotW = zoneW - gonioW;
    g.setColour (pal.ring.withAlpha (0.6f));
    g.drawHorizontalLine ((int) cy, 0.0f, plotW);
    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.5f, juce::Font::bold)));
    g.drawText ("PHASE", 50, (int) Hm + 4, 60, 12, juce::Justification::left);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain)));
    g.drawText ("+180", 12, (int) (cy - ph) - 6, 40, 12, juce::Justification::left);
    g.drawText ("0", 12, (int) cy - 6, 40, 12, juce::Justification::left);
    g.drawText ("-180", 12, (int) (cy + ph) - 6, 40, 12, juce::Justification::left);

    const bool linear = proc.readMode() == eqd::PhaseMode::Linear;
    juce::RectangleList<float> phaseDots;
    for (int i = 0; i < kPoints; ++i)
    {
        float d = phaseDeg[(size_t) i];
        d = std::fmod (d + 180.0f, 360.0f); if (d < 0.0f) d += 360.0f; d -= 180.0f;      // wrapped to +-180
        const float x = (float) i / (float) (kPoints - 1) * plotW;
        phaseDots.addWithoutMerging ({ x, cy - d / 180.0f * ph, 1.6f, 1.6f });
    }
    g.setColour (pal.fg.withAlpha (0.85f));
    g.fillRectList (phaseDots);
    if (linear)
    {
        g.setColour (pal.dim);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 10.0f, juce::Font::italic)));
        g.drawText (juce::String::fromUTF8 ("LINEAIRE : phase nulle, aucun d\xC3\xA9phasage"), (int) (plotW * 0.5f) - 130, (int) cy - 18, 260, 12,
                    juce::Justification::centred);
    }

    // goniometer
    const float gx = zoneW - gonioW * 0.5f, gy = cy, gs = 46.0f;
    g.setColour (pal.ring);
    g.drawVerticalLine ((int) (zoneW - gonioW), Hm, H);
    g.drawRect (gx - gs, gy - gs, gs * 2.0f, gs * 2.0f, 1.0f);
    g.setColour (pal.ring.withAlpha (0.6f));
    g.drawLine (gx - gs, gy, gx + gs, gy, 1.0f);
    g.drawLine (gx, gy - gs, gx, gy + gs, 1.0f);
    const float sc = gs * 0.92f / (1.4142f * std::max (gonioPeak, 0.02f));
    juce::RectangleList<float> gd;
    for (const auto& q : gonio)
    {
        const float px = gx + juce::jlimit (-gs, gs, q.x * sc), py = gy - juce::jlimit (-gs, gs, q.y * sc);
        gd.addWithoutMerging ({ px, py, 1.4f, 1.4f });
    }
    g.setColour (pal.fg.withAlpha (0.75f));
    g.fillRectList (gd);
    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain)));
    g.drawText ("L", (int) (gx - gs) - 12, (int) (gy - gs) - 2, 12, 12, juce::Justification::centred);
    g.drawText ("R", (int) (gx + gs), (int) (gy - gs) - 2, 12, 12, juce::Justification::centred);
    g.setColour (pal.fg);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
    g.drawText ("CORR " + juce::String (corr >= 0.0f ? "+" : "") + juce::String (corr, 2), (int) (zoneW - gonioW) + 4, (int) (H - 14), (int) gonioW - 8, 12,
                juce::Justification::centred);

    drawMeters (g);
}

void DuneField::drawMeters (juce::Graphics& g)
{
    const auto P = meterPanel;
    g.setColour (pal.bg.withAlpha (0.90f));
    g.fillRect (P);
    g.setColour (pal.ring);
    g.drawRect (P, 1.0f);

    auto fnum = [] (float v, int dp = 1) { return v <= -99.0f ? juce::String ("--") : juce::String (v, dp); };
    auto fdiff = [] (float a, float b)
    {
        if (a <= -99.0f || b <= -99.0f) return juce::String ("--");
        const float d = b - a; return (d > 0.05f ? juce::String ("+") : juce::String()) + juce::String (d, 1);
    };

    // ---------------- round Nagra dial: silver bezel, white face, three arcs (peak / VU / gain reduction), one needle each
    const juce::Point<float> c (P.getX() + 8.0f + 72.0f, P.getCentreY());
    const float bezel = 72.0f, face = 66.0f;
    g.setGradientFill (juce::ColourGradient (pal.fg, c.x - bezel, c.y - bezel, pal.fg.withAlpha (0.25f), c.x + bezel, c.y + bezel, false));
    g.fillEllipse (c.x - bezel, c.y - bezel, bezel * 2.0f, bezel * 2.0f);
    g.setColour (pal.bg);
    g.fillEllipse (c.x - face - 1.5f, c.y - face - 1.5f, (face + 1.5f) * 2.0f, (face + 1.5f) * 2.0f);
    g.setColour (pal.fg.withAlpha (0.96f));
    g.fillEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f);

    const juce::Colour ink = pal.bg;
    const juce::Point<float> pivot (c.x, c.y + 44.0f);
    const float span = 34.0f;
    auto ang = [&] (float frac) { return juce::degreesToRadians (-span + 2.0f * span * juce::jlimit (0.0f, 1.0f, frac)); };
    auto pt = [&] (float radius, float a) { return juce::Point<float> (pivot.x + radius * std::sin (a), pivot.y - radius * std::cos (a)); };
    auto fmt = [] (float v) { return v > 0.0f ? "+" + juce::String ((int) v) : juce::String ((int) v); };

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f);
        g.reduceClipRegion (clip);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 6.8f, juce::Font::bold)));

        auto arc = [&] (float R, float lo, float hi, std::initializer_list<float> ticks, float minorStep, bool grScale)
        {
            juce::Path p;
            p.addCentredArc (pivot.x, pivot.y, R, R, 0.0f, juce::degreesToRadians (-span), juce::degreesToRadians (span), true);
            g.setColour (ink);
            g.strokePath (p, juce::PathStrokeType (1.1f));
            for (float v = lo; v <= hi + 0.01f; v += minorStep)
                g.drawLine ({ pt (R, ang ((v - lo) / (hi - lo))), pt (R - 2.5f, ang ((v - lo) / (hi - lo))) }, 0.7f);
            for (float v : ticks)
            {
                const float a = ang ((v - lo) / (hi - lo));
                g.setColour (ink);
                g.drawLine ({ pt (R, a), pt (R - 5.0f, a) }, 1.3f);
                const auto tp = pt (R + 6.5f, a);
                g.drawText (grScale ? juce::String ((int) v) : fmt (v), (int) tp.x - 11, (int) tp.y - 5, 22, 10, juce::Justification::centred);
            }
        };
        arc (82.0f, -30.0f, 3.0f, { -30.0f, -20.0f, -10.0f, -6.0f, -3.0f, 0.0f, 3.0f }, 1.0f, false);   // PEAK, dBFS
        arc (63.0f, -20.0f, 3.0f, { -20.0f, -10.0f, -5.0f, 0.0f, 3.0f }, 1.0f, false);                  // VU (0 VU = -18 dBFS)
        arc (44.0f, 0.0f, 25.0f, { 0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f }, 1.0f, true);               // gain reduction, dB

        const float peakDb = std::max (proc.meters[0].ppm.load(), proc.meters[1].ppm.load());
        const float vuDb   = std::max (proc.meters[0].vu.load(),  proc.meters[1].vu.load()) + 18.0f;
        const float gr     = proc.compGrDb.load();
        auto needle = [&] (float radius, float frac, float thick)
        {
            const float a = ang (frac);
            g.setColour (ink);
            g.drawLine ({ pivot, pt (radius, a) }, thick);
        };
        needle (86.0f, (peakDb + 30.0f) / 33.0f, 1.9f);        // PEAK, thick
        needle (67.0f, (vuDb + 20.0f) / 23.0f, 1.0f);          // VU slow, thin
        needle (48.0f, gr / 25.0f, 1.4f);                      // gain reduction

        g.setColour (ink);
        g.fillEllipse (pivot.x - 6.0f, pivot.y - 6.0f, 12.0f, 12.0f);                        // hub
        g.setColour (pal.fg);
        g.fillEllipse (pivot.x - 2.0f, pivot.y - 2.0f, 4.0f, 4.0f);
        g.setColour (ink);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 7.5f, juce::Font::bold)));
        g.drawText ("MODULOMETER", (int) c.x - 40, (int) c.y + 14, 80, 10, juce::Justification::centred);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 6.5f, juce::Font::plain)));
        g.drawText ("PEAK  /  VU  /  GR dB", (int) c.x - 44, (int) c.y + 24, 88, 9, juce::Justification::centred);
    }

    // ---------------- readouts: input / output / difference
    const float tx = P.getX() + 160.0f;
    const float colX[4] { tx, tx + 78.0f, tx + 158.0f, tx + 238.0f };
    auto mono = juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    auto small = juce::Font (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::bold));
    float y = P.getY() + 3.0f;
    g.setColour (pal.dim);
    g.setFont (small);
    g.drawText ("clic = reset", (int) colX[0], (int) y, 80, 12, juce::Justification::left);
    g.drawText ("ENTREE", (int) colX[1], (int) y, 70, 12, juce::Justification::right);
    g.drawText ("SORTIE", (int) colX[2], (int) y, 70, 12, juce::Justification::right);
    g.drawText ("DIFF", (int) colX[3], (int) y, 70, 12, juce::Justification::right);

    const float outRms = 10.0f * std::log10 (0.5f * (std::pow (10.0f, proc.meters[0].rms.load() / 10.0f) + std::pow (10.0f, proc.meters[1].rms.load() / 10.0f)));
    const float inPk = proc.inPeakDb.load();
    const float outTp = std::max (proc.meters[0].truePeak.load(), proc.meters[1].truePeak.load());
    const float inI = proc.inLoud.integrated.load(), outI = proc.outLoud.integrated.load();
    struct Row { const char* name; float in, out; };
    const Row rows[] {
        { "RMS dBFS", proc.inRmsDb.load(), outRms },
        { "LUFS-M",   proc.inLoud.momentary.load(), proc.outLoud.momentary.load() },
        { "LUFS-C",   proc.inLoud.shortTerm.load(), proc.outLoud.shortTerm.load() },
        { "LUFS-I",   inI, outI },
        { "PEAK/TP",  inPk, outTp },
        { "DR (PLR)", (inPk > -99.0f && inI > -99.0f) ? inPk - inI : -120.0f, (outTp > -99.0f && outI > -99.0f) ? outTp - outI : -120.0f },
    };
    y += 14.0f;
    for (const auto& rw : rows)
    {
        g.setColour (pal.dim);
        g.setFont (small);
        g.drawText (rw.name, (int) colX[0], (int) y, 80, 13, juce::Justification::left);
        g.setColour (pal.fg);
        g.setFont (mono);
        const bool isTp = std::strcmp (rw.name, "PEAK/TP") == 0;
        g.drawText (fnum (rw.in), (int) colX[1], (int) y, 70, 13, juce::Justification::right);
        g.setColour (isTp && rw.out > -1.0f ? juce::Colour (0xffe0463c) : pal.fg);
        g.drawText (fnum (rw.out), (int) colX[2], (int) y, 70, 13, juce::Justification::right);
        g.setColour (pal.fg);
        g.drawText (fdiff (rw.in, rw.out), (int) colX[3], (int) y, 70, 13, juce::Justification::right);
        y += 13.0f;
    }
    g.setColour (pal.dim);
    g.setFont (small);
    const double gainDb = proc.apvts.getRawParameterValue ("out")->load();
    g.drawText ("GAIN SORTIE " + juce::String (gainDb >= 0.05 ? "+" : "") + juce::String (gainDb, 1) + " dB", (int) colX[0], (int) y, 150, 12, juce::Justification::left);
    g.drawText ("TP " + juce::String (kOsNames[juce::jlimit (0, 5, (int) std::lround (proc.apvts.getRawParameterValue ("os")->load()))]), (int) colX[3], (int) y, 70, 12, juce::Justification::right);

    // ---------------- dynamic-range spreader: loudness -> peak, input over output
    y += 14.0f;
    const float bx = colX[0] + 50.0f, bw = 270.0f;
    auto xOf = [&] (float db) { return bx + bw * juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 40.0f); };
    g.setColour (pal.ring);
    g.drawLine (bx, y + 21.0f, bx + bw, y + 21.0f, 1.0f);
    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 8.0f, juce::Font::plain)));
    for (int db = -40; db <= 0; db += 10)
    {
        g.drawLine (xOf ((float) db), y + 21.0f, xOf ((float) db), y + 24.0f, 1.0f);
        g.drawText (juce::String (db), (int) xOf ((float) db) - 12, (int) y + 23, 24, 9, juce::Justification::centred);
    }
    auto bar = [&] (float lo, float hi, float yy, const char* name, bool filled)
    {
        g.setColour (pal.dim);
        g.setFont (small);
        g.drawText (name, (int) colX[0], (int) yy - 2, 50, 11, juce::Justification::left);
        if (lo <= -99.0f || hi <= -99.0f) return;
        g.setColour (pal.fg);
        const float x0 = xOf (lo), x1 = xOf (hi);
        if (filled) g.fillRect (x0, yy, std::max (x1 - x0, 1.0f), 6.0f); else g.drawRect (x0, yy, std::max (x1 - x0, 1.0f), 6.0f, 1.0f);
        g.fillRect (x0 - 1.0f, yy - 2.0f, 2.0f, 10.0f);
        g.fillRect (x1 - 1.0f, yy - 2.0f, 2.0f, 10.0f);
    };
    bar (inI, inPk, y + 1.0f, "ENTREE", false);
    bar (outI, outTp, y + 11.0f, "SORTIE", true);
}

// ===========================================================================
// One band: number/on, type, big frequency knob, gain + Q knobs (or slope), ST/M/S
BandStrip::BandStrip (MasteringEQAudioProcessor& p, int index, const Palette& pl) : proc (p), band (index), pal (pl)
{
    auto id = [index] (const char* n) { return MasteringEQAudioProcessor::pid (index, n); };

    numBtn.setButtonText (juce::String (index + 1).paddedLeft ('0', 2));
    addAndMakeVisible (numBtn);
    fillCombo (proc.apvts, id ("type"), typeBox);
    fillCombo (proc.apvts, id ("slope"), slopeBox);
    fillCombo (proc.apvts, id ("fam"), famBox);
    addAndMakeVisible (famBox);
    addAndMakeVisible (typeBox);
    addAndMakeVisible (slopeBox);

    for (auto* s : { &freqKnob, &gainKnob, &qKnob })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity (220);
        addAndMakeVisible (s);
    }
    gainKnob.getProperties().set ("bipolar", true);

    onAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>   (proc.apvts, id ("on"),    numBtn);
    typeAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id ("type"),  typeBox);
    slopeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id ("slope"), slopeBox);
    famAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id ("fam"), famBox);
    freqAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>   (proc.apvts, id ("freq"),  freqKnob);
    gainAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>   (proc.apvts, id ("gain"),  gainKnob);
    qAtt     = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>   (proc.apvts, id ("q"),     qKnob);

    stBtn.setButtonText ("ST"); mBtn.setButtonText ("M"); sBtn.setButtonText ("S");
    stBtn.onClick = [this] { setChannel (0); };
    mBtn.onClick  = [this] { setChannel (3); };
    sBtn.onClick  = [this] { setChannel (4); };
    for (auto* b : { &stBtn, &mBtn, &sBtn }) addAndMakeVisible (b);
    refresh();
}

void BandStrip::setChannel (int idx)
{
    setParamNormalised (proc.apvts, MasteringEQAudioProcessor::pid (band, "ch"), (float) idx);
    refresh();
}

void BandStrip::refresh()
{
    const auto b = proc.readBand (band);
    bandOn = b.on;
    isPass = b.type == eqd::BandType::HighPass || b.type == eqd::BandType::LowPass;
    hasGain = b.type == eqd::BandType::Bell || b.type == eqd::BandType::LowShelf || b.type == eqd::BandType::HighShelf;

    slopeBox.setVisible (isPass);
    famBox.setVisible (isPass);
    gainKnob.setVisible (hasGain);
    qKnob.setVisible (true);
    if (isPass) qKnob.setBounds (10, 188, 52, 50); else qKnob.setBounds (79, 143, 58, 58);

    const int ch = (int) b.ch;
    stBtn.setToggleState (ch == 0, juce::dontSendNotification);
    mBtn.setToggleState (ch == 3, juce::dontSendNotification);
    sBtn.setToggleState (ch == 4, juce::dontSendNotification);

    const float a = bandOn ? 1.0f : 0.38f;
    for (auto* c : std::initializer_list<juce::Component*> { &typeBox, &slopeBox, &famBox, &freqKnob, &gainKnob, &qKnob, &stBtn, &mBtn, &sBtn })
        c->setAlpha (a);
    repaint();
}

void BandStrip::resized()
{
    numBtn.setBounds (8, 10, 30, 22);
    typeBox.setBounds (42, 10, 100, 22);
    freqKnob.setBounds (38, 40, 74, 74);
    famBox.setBounds (8, 142, 134, 20);
    slopeBox.setBounds (8, 165, 134, 20);
    gainKnob.setBounds (12, 143, 58, 58);
    qKnob.setBounds (79, 143, 58, 58);
    stBtn.setBounds (9, 238, 41, 19);
    mBtn.setBounds (54, 238, 41, 19);
    sBtn.setBounds (98, 238, 41, 19);
}

void BandStrip::paint (juce::Graphics& g)
{
    const float a = bandOn ? 1.0f : 0.4f;
    g.setColour (pal.ring);
    g.drawVerticalLine (getWidth() - 1, 10.0f, (float) getHeight());

    const juce::Font f (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::plain));
    juce::AttributedString as;
    as.setJustification (juce::Justification::centred);
    as.append (noteName (freqKnob.getValue(), proc.readTuning()) + "  ", f, pal.dim.withAlpha (a));
    as.append (fmtFreq (freqKnob.getValue()), f, pal.fg.withAlpha (a));
    as.draw (g, juce::Rectangle<float> (0.0f, 121.0f, 150.0f, 18.0f));

    g.setFont (f);
    if (hasGain)
    {
        const double v = gainKnob.getValue();
        g.setColour (pal.fg.withAlpha (a));
        g.drawText ((v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB", 6, 205, 70, 18, juce::Justification::centred);
    }
    g.setColour (pal.fg.withAlpha (a));
    if (isPass) g.drawText ("Q " + juce::String (qKnob.getValue(), 2), 66, 204, 78, 18, juce::Justification::centredLeft);
    else        g.drawText ("Q " + juce::String (qKnob.getValue(), 2), 73, 205, 70, 18, juce::Justification::centred);
}

// ===========================================================================
// Split compressor panel
void PresetGlyph::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (pal.bg.withAlpha (0.9f));
    g.fillRect (b);
    g.setColour (highlighted ? pal.fg : pal.dim);
    g.drawRect (b, 1.0f);
    juce::Path p;
    const float x0 = b.getX() + 4.0f, x1 = b.getRight() - 4.0f, yt = b.getY() + 4.0f, yb = b.getBottom() - 4.0f, w = x1 - x0;
    if (kind == 0)      { p.startNewSubPath (x0, yt); p.lineTo (x0 + w * 0.5f, yt); p.lineTo (x0 + w * 0.8f, yb); p.lineTo (x1, yb); }   // low band only
    else if (kind == 1) { p.startNewSubPath (x0, yb); p.lineTo (x0 + w * 0.25f, yb); p.lineTo (x0 + w * 0.4f, yt); p.lineTo (x0 + w * 0.6f, yt); p.lineTo (x0 + w * 0.75f, yb); p.lineTo (x1, yb); }   // middle
    else                { p.startNewSubPath (x0, yb); p.lineTo (x0 + w * 0.2f, yb); p.lineTo (x0 + w * 0.5f, yt); p.lineTo (x1, yt); }   // high band only
    g.setColour (pal.fg);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

CompPanel::CompPanel (MasteringEQAudioProcessor& p, const Palette& pl)
    : proc (p), pal (pl), onBtn ("SPLIT COMP", pl), postBtn ("POST EQ", pl), minBtn ("LINEAIRE", pl), linBtn ("LOI LOG", pl),
      pLow (0, pl), pMid (1, pl), pHigh (2, pl)
{
    static const char* ids[8] { "comp_lo", "comp_hi", "comp_thr", "comp_ratio", "comp_att", "comp_rel", "comp_knee", "comp_mk" };
    for (int i = 0; i < 8; ++i)
    {
        auto& s = knob[i];
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s.setMouseDragSensitivity (220);
        addAndMakeVisible (s);
        knobAtt[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids[i], s);
    }
    knob[7].getProperties().set ("bipolar", true);
    for (auto* b : { &onBtn, &postBtn, &minBtn, &linBtn }) addAndMakeVisible (b);
    for (auto* c : { &msBox, &sloBox, &shiBox }) addAndMakeVisible (c);
    for (auto* b : { &pLow, &pMid, &pHigh }) addAndMakeVisible (b);
    pLow.setTooltip ("Grave seulement");  pMid.setTooltip ("Medium seulement");  pHigh.setTooltip ("Aigu seulement");
    pLow.onClick  = [this] { setParamValue (proc.apvts, "comp_lo", 20.0f);   setParamValue (proc.apvts, "comp_hi", 250.0f); };
    pMid.onClick  = [this] { setParamValue (proc.apvts, "comp_lo", 300.0f);  setParamValue (proc.apvts, "comp_hi", 3000.0f); };
    pHigh.onClick = [this] { setParamValue (proc.apvts, "comp_lo", 3000.0f); setParamValue (proc.apvts, "comp_hi", 20000.0f); };
    fillCombo (proc.apvts, "comp_ms", msBox);
    fillCombo (proc.apvts, "comp_slo", sloBox);
    fillCombo (proc.apvts, "comp_shi", shiBox);
    msAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "comp_ms", msBox);
    sloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "comp_slo", sloBox);
    shiAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "comp_shi", shiBox);
    onAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "comp_on", onBtn);
    postAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "comp_post", postBtn);
    linAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "comp_lin", linBtn);
    minAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "comp_min", minBtn);
}

void CompPanel::resized()
{
    onBtn.setBounds (6, 8, 100, 26);
    postBtn.setBounds (110, 8, 82, 26);
    minBtn.setBounds (196, 8, 92, 26);
    linBtn.setBounds (130, 42, 104, 20);
    msBox.setBounds (292, 9, 78, 24);
    sloBox.setBounds (374, 9, 82, 24);
    shiBox.setBounds (460, 9, 82, 24);
    pLow.setBounds (10, 42, 32, 18);
    pMid.setBounds (46, 42, 32, 18);
    pHigh.setBounds (82, 42, 32, 18);
    for (int i = 0; i < 8; ++i) knob[i].setBounds (8 + 66 * i + 4, 100, 54, 54);
}

void CompPanel::refresh()
{
    postBtn.setLabel (postBtn.getToggleState() ? "POST EQ" : "PRE EQ");
    minBtn.setLabel (minBtn.getToggleState() ? "MINIMALE" : "LINEAIRE");
    linBtn.setLabel (linBtn.getToggleState() ? "LOI LINEAIRE" : "LOI LOG (dB)");
    const float a = onBtn.getToggleState() ? 1.0f : 0.45f;
    for (auto& k : knob) k.setAlpha (a);
    repaint();
}

void CompPanel::paint (juce::Graphics& g)
{
    g.setColour (pal.bg.withAlpha (0.90f));
    g.fillRect (getLocalBounds());
    g.setColour (pal.ring);
    g.drawRect (getLocalBounds(), 1);

    // the zone that gets compressed, on the same frequency axis as the sand (20 Hz - 20 kHz)
    const juce::Rectangle<float> gr (8.0f, 40.0f, 530.0f, 54.0f);
    g.setColour (pal.ring);
    g.drawRect (gr, 1.0f);
    for (double f : { 100.0, 1000.0, 10000.0 })
    {
        const float x = gr.getX() + gr.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
        g.setColour (pal.ring.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, gr.getY() + 1.0f, gr.getBottom() - 1.0f);
        g.setColour (pal.dim);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 8.0f, juce::Font::plain)));
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), (int) x - 12, (int) gr.getBottom() - 10, 24, 9, juce::Justification::centred);
    }
    juce::Path fill, line;
    const int npts = 265;
    for (int i = 0; i < npts; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, (double) i / (double) (npts - 1));
        const float gain = (float) compBandGain (proc.apvts, f);
        const float x = gr.getX() + gr.getWidth() * (float) i / (float) (npts - 1);
        const float y = gr.getBottom() - 12.0f - (gr.getHeight() - 22.0f) * gain;
        if (i == 0) { fill.startNewSubPath (x, gr.getBottom() - 12.0f); fill.lineTo (x, y); line.startNewSubPath (x, y); }
        else        { fill.lineTo (x, y); line.lineTo (x, y); }
    }
    fill.lineTo (gr.getRight(), gr.getBottom() - 12.0f);
    fill.closeSubPath();
    g.setColour (pal.fg.withAlpha (onBtn.getToggleState() ? 0.30f : 0.12f));
    g.fillPath (fill);
    g.setColour (pal.fg);
    g.strokePath (line, juce::PathStrokeType (1.5f));
    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 8.5f, juce::Font::bold)));
    g.drawText ("ZONE COMPRIMEE", (int) gr.getRight() - 120, (int) gr.getY() + 3, 114, 10, juce::Justification::right);

    static const char* names[8] { "DEPUIS", "JUSQU'A", "SEUIL", "RATIO", "ATTAQUE", "RELACHE", "KNEE", "GAIN" };
    auto v = [this] (const char* id) { return (double) paramValue (proc.apvts, id); };
    auto fq = [] (double f, bool lo) { return (lo && f <= 20.5) || (! lo && f >= 19999.0) ? juce::String ("ouvert") : (f >= 1000.0 ? juce::String (f / 1000.0, 2) + " kHz" : juce::String (f, 0) + " Hz"); };
    juce::String txt[8] {
        fq (v ("comp_lo"), true), fq (v ("comp_hi"), false), juce::String (v ("comp_thr"), 1) + " dB", juce::String (v ("comp_ratio"), 1) + ":1",
        v ("comp_att") < 10.0 ? juce::String (v ("comp_att"), 1) + " ms" : juce::String ((int) v ("comp_att")) + " ms",
        v ("comp_rel") >= 1000.0 ? juce::String (v ("comp_rel") / 1000.0, 2) + " s" : juce::String ((int) v ("comp_rel")) + " ms",
        juce::String (v ("comp_knee"), 1) + " dB", (v ("comp_mk") > 0.05 ? "+" : "") + juce::String (v ("comp_mk"), 1) + " dB" };
    for (int i = 0; i < 8; ++i)
    {
        const int x = 8 + 66 * i;
        g.setColour (pal.dim);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::bold)));
        g.drawText (names[i], x, 155, 66, 11, juce::Justification::centred);
        g.setColour (pal.fg);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 10.5f, juce::Font::plain)));
        g.drawText (txt[i], x, 166, 66, 13, juce::Justification::centred);
    }
}

// ===========================================================================
SrcPanel::SrcPanel (MasteringEQAudioProcessor& p, const Palette& pl) : proc (p), pal (pl)
{
    addAndMakeVisible (osBox);
    addAndMakeVisible (steepBox);
    fillCombo (proc.apvts, "proc_os", osBox);
    fillCombo (proc.apvts, "src_steep", steepBox);
    osAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "proc_os", osBox);
    steepAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "src_steep", steepBox);
    for (auto* s : { &cutKnob, &preKnob })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity (220);
        addAndMakeVisible (s);
    }
    cutAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "src_cut", cutKnob);
    preAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "src_pre", preKnob);
}

void SrcPanel::resized()
{
    osBox.setBounds (8, 30, 70, 24);
    steepBox.setBounds (86, 30, 112, 24);
    cutKnob.setBounds (214, 14, 52, 52);
    preKnob.setBounds (386, 14, 52, 52);
}

void SrcPanel::paint (juce::Graphics& g)
{
    g.setColour (pal.bg.withAlpha (0.86f));
    g.fillRect (getLocalBounds());
    g.setColour (pal.ring);
    g.drawRect (getLocalBounds(), 1);
    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.5f, juce::Font::bold)));
    g.drawText ("OVERSAMPLING", 8, 6, 78, 12, juce::Justification::left);
    g.drawText ("FILTRE UP / DOWN", 134, 6, 120, 12, juce::Justification::left);
    g.drawText ("PENTE", 86, 56, 112, 11, juce::Justification::left);
    g.drawText ("COUPURE", 272, 20, 100, 11, juce::Justification::left);
    g.drawText ("PRE-RINGING", 444, 20, 100, 11, juce::Justification::left);
    const double cut = paramValue (proc.apvts, "src_cut"), pre = paramValue (proc.apvts, "src_pre");
    g.setColour (pal.fg);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::plain)));
    g.drawText (juce::String (cut / 1000.0, 2) + " kHz", 272, 34, 100, 15, juce::Justification::left);
    g.drawText (juce::String ((int) pre) + " %" + (pre >= 99.5 ? "  lineaire" : (pre <= 0.5 ? "  minimale" : "")), 444, 34, 100, 15, juce::Justification::left);
    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain)));
    g.drawText (juce::String::fromUTF8 ("100 % = phase lin\xC3\xA9" "aire (pr\xC3\xA9-ringing sym\xC3\xA9trique) \xC2\xB7 0 % = phase minimale (aucun pr\xC3\xA9-ringing)"),
                214, 64, 330, 12, juce::Justification::left);
}

// ===========================================================================
DuneContent::DuneContent (MasteringEQAudioProcessor& p) : proc (p), field (p, palette), curvesBtn ("COURBES", palette), curvesTopBtn ("DESSUS", palette), tiltBtn ("TILT", palette), bypassBtn ("BYPASS", palette), lookBtn ("ANTICIPATION", palette), lineBtn ("TILT DROIT", palette), comp (p, palette), src (p, palette)
{
    setLookAndFeel (&lnf);
    lnf.setPalette (palette);

    const char* modeNames[3] = { "LINEAIRE", "NATURELLE", "MINIMALE" };
    for (int i = 0; i < 3; ++i)
    {
        modeBtn[i].setButtonText (modeNames[i]);
        modeBtn[i].onClick = [this, i] { setParamIndex ("mode", i); };
        addAndMakeVisible (modeBtn[i]);
    }

    fillCombo (proc.apvts, "quality", qualityBox);
    fillCombo (proc.apvts, "tuning", tuningBox);
    fillCombo (proc.apvts, "theme", themeBox);
    for (auto* c : { &qualityBox, &tuningBox, &themeBox }) addAndMakeVisible (c);
    qualityAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "quality", qualityBox);
    tuningAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "tuning", tuningBox);
    themeAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "theme", themeBox);

    for (auto* s : { &tiltSlider, &outSlider })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity (200);
        addAndMakeVisible (s);
    }
    tiltSliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "tilt_db", tiltSlider);
    outAtt        = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "out", outSlider);

    addAndMakeVisible (tiltBtn);
    addAndMakeVisible (bypassBtn);
    addAndMakeVisible (curvesBtn);
    addAndMakeVisible (curvesTopBtn);
    addAndMakeVisible (lookBtn);
    addAndMakeVisible (osBox);
    addAndMakeVisible (src);
    addAndMakeVisible (lineBtn);
    for (auto* s : { &lineSlider, &linePivot })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity (200);
        addAndMakeVisible (s);
    }
    lineAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "tilt_line", lineBtn);
    lineSAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "line_db", lineSlider);
    linePAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "line_freq", linePivot);
    addAndMakeVisible (comp);
    fillCombo (proc.apvts, "os", osBox);
    osAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "os", osBox);
    lookAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "look", lookBtn);
    curvesAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "curves_on", curvesBtn);
    curvesTopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "curves_top", curvesTopBtn);
    tiltAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "tilt_on", tiltBtn);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassBtn);

    plusBtn.onClick = [this]
    {
        for (int i = 0; i < eqd::kMaxBands; ++i)
            if (! proc.readBand (i).on) { setParamNormalised (proc.apvts, MasteringEQAudioProcessor::pid (i, "on"), 1.0f); break; }
    };
    minusBtn.onClick = [this]
    {
        for (int i = eqd::kMaxBands - 1; i >= 0; --i)
            if (proc.readBand (i).on) { setParamNormalised (proc.apvts, MasteringEQAudioProcessor::pid (i, "on"), 0.0f); break; }
    };
    addAndMakeVisible (plusBtn);
    addAndMakeVisible (minusBtn);

    addAndMakeVisible (field);
    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        strips.push_back (std::make_unique<BandStrip> (proc, i, palette));
        addAndMakeVisible (*strips.back());
    }

    applyTheme();
    startTimerHz (15);
}

DuneContent::~DuneContent()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DuneContent::setParamIndex (const char* id, int idx)
{
    setParamNormalised (proc.apvts, id, (float) idx);
}

void DuneContent::applyTheme()
{
    const int th = juce::jlimit (0, 2, (int) std::lround (paramValue (proc.apvts, "theme")));
    palette = paletteFor (th);
    lnf.setPalette (palette);
    sendLookAndFeelChange();
    repaint();
}

void DuneContent::timerCallback()
{
    const int th = (int) std::lround (paramValue (proc.apvts, "theme"));
    if (th != lastTheme) { lastTheme = th; applyTheme(); }

    const int mode = (int) std::lround (paramValue (proc.apvts, "mode"));
    for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState (i == mode, juce::dontSendNotification);
    for (auto& s : strips) s->refresh();
    curvesTopBtn.setLabel (curvesTopBtn.getToggleState() ? "DESSUS" : "DESSOUS");
    curvesTopBtn.setEnabled (curvesBtn.getToggleState());
    comp.refresh();

    const double fs = std::max (proc.getSampleRate(), 1.0);
    footerText = juce::String::fromUTF8 ("64-bit  \xC2\xB7  latence ")
                 + juce::String ((double) proc.getLatencyForGui() / fs, 2) + " s";
    repaint (juce::Rectangle<int> (0, 0, (int) kW, 44));
    repaint (juce::Rectangle<int> (0, 754, (int) kW, 36));
}

void DuneContent::resized()
{
    const int y = 11, h = 24;
    modeBtn[0].setBounds (262, y, 86, h);
    modeBtn[1].setBounds (348, y, 86, h);
    modeBtn[2].setBounds (434, y, 86, h);
    qualityBox.setBounds (532, y, 128, h);
    tuningBox.setBounds (668, y, 92, h);
    tiltBtn.setBounds (770, y, 62, h);
    tiltSlider.setBounds (838, 4, 36, 36);
    outSlider.setBounds (1058, 4, 36, 36);
    bypassBtn.setBounds (1148, y, 86, h);

    field.setBounds (0, 45, (int) kW, 418);
    for (auto* b : { &curvesBtn, &curvesTopBtn, &lookBtn }) b->toFront (false);
    osBox.toFront (false);
    src.setBounds (690, 45 + 186, 546, 78);
    src.toFront (false);
    lineBtn.setBounds (386, 761, 112, 24);
    lineSlider.setBounds (506, 758, 30, 30);
    linePivot.setBounds (700, 758, 30, 30);
    comp.setBounds (690, 45 + 6, 546, 180);
    comp.toFront (false);
    lookBtn.setBounds (150, 761, 128, 24);
    osBox.setBounds (322, 761, 58, 24);
    curvesBtn.setBounds (14, 52, 100, 26);
    curvesTopBtn.setBounds (124, 52, 100, 26);
    for (int i = 0; i < eqd::kMaxBands; ++i) strips[(size_t) i]->setBounds (150 * i, 465, 150, 287);
    plusBtn.setBounds (1206, 475, 28, 28);
    minusBtn.setBounds (1206, 515, 28, 28);
    themeBox.setBounds (1010, 762, 130, 22);
}

void DuneContent::paint (juce::Graphics& g)
{
    g.fillAll (palette.bg);
    g.setColour (palette.fg);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 22.0f, juce::Font::plain)));
    g.drawText ("D U N E", 16, 8, 200, 30, juce::Justification::left);

    g.setColour (palette.ring);
    g.drawHorizontalLine (44, 0.0f, kW);
    g.drawHorizontalLine (464, 0.0f, kW);
    g.drawVerticalLine (1200, 475.0f, 752.0f);
    g.drawHorizontalLine (753, 0.0f, kW);

    const juce::Font f (juce::FontOptions ("Helvetica Neue", 11.5f, juce::Font::plain));
    g.setFont (f);
    g.setColour (palette.fg);
    const double tilt = paramValue (proc.apvts, "tilt_db");
    g.drawText ((tilt > 0.05 ? "+" : "") + juce::String (tilt, 1), 878, 11, 48, 24, juce::Justification::centredLeft);
    g.drawText ("SORTIE", 960, 11, 66, 24, juce::Justification::centredRight);
    g.drawText (juce::String (paramValue (proc.apvts, "out"), 1), 1098, 11, 46, 24, juce::Justification::centredLeft);

    {
        const double sl = paramValue (proc.apvts, "line_db"), pv = paramValue (proc.apvts, "line_freq");
        g.setColour (palette.fg);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 11.5f, juce::Font::plain)));
        g.drawText ((sl > 0.005 ? "+" : "") + juce::String (sl, 2) + " dB/oct", 540, 761, 82, 24, juce::Justification::centredLeft);
        g.drawText (pv >= 1000.0 ? juce::String (pv / 1000.0, 2) + " kHz" : juce::String ((int) pv) + " Hz", 734, 761, 80, 24, juce::Justification::centredLeft);
    }
    g.setColour (palette.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 11.0f, juce::Font::plain)));
    g.drawText (footerText, 16, 762, 600, 22, juce::Justification::centredLeft);
    g.setColour (palette.fg);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 12.5f, juce::Font::italic)));
    g.drawText ("ImagoMusica", (int) kW - 176, 762, 160, 22, juce::Justification::centredRight);
}

// ===========================================================================
MasteringEQAudioProcessorEditor::MasteringEQAudioProcessorEditor (MasteringEQAudioProcessor& p)
    : AudioProcessorEditor (&p), content (p)
{
    addAndMakeVisible (content);
    setSize ((int) kW, (int) kH);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setResizeLimits (620, 395, 2480, 1580);
}

void MasteringEQAudioProcessorEditor::resized()
{
    content.setBounds (0, 0, (int) kW, (int) kH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / kW));
}
