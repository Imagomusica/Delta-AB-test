#include <cstring>
#include <cstdlib>
#include "PluginEditor.h"

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
    const float r = (float) juce::jmin (w, h) * 0.5f - 3.0f;
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
    const float a = startAngle + pos * (endAngle - startAngle);

    juce::Path ring;
    ring.addCentredArc (cx, cy, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (pal.ring);
    g.strokePath (ring, juce::PathStrokeType (1.4f));

    const bool bipolar = (bool) slider.getProperties()["bipolar"];
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path val;
    val.addCentredArc (cx, cy, r, r, 0.0f, std::min (from, a), std::max (from, a), true);
    g.setColour (pal.fg);
    g.strokePath (val, juce::PathStrokeType (2.6f));

    g.drawLine (cx + std::sin (a) * r * 0.32f, cy - std::cos (a) * r * 0.32f,
                cx + std::sin (a) * r * 0.78f, cy - std::cos (a) * r * 0.78f, 2.0f);
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
    meterPanel = { 14.0f, 10.0f, 640.0f, 200.0f };
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
    const double fs = std::max (proc.getSampleRate(), 8000.0);
    for (int i = 0; i < kPoints; ++i)
    {
        const double u = (double) i / (double) (kPoints - 1);
        const double f = 3.0 * std::pow (10000.0, u);
        dbL[(size_t) i] = (float) eqd::totalMagnitudeDb (bands, eqd::kMaxBands, eqd::Channel::Left, f, &tilt);
        dbM[(size_t) i] = (float) eqd::totalMagnitudeDb (bands, eqd::kMaxBands, eqd::Channel::Mid, f, nullptr) + dbL[(size_t) i];
        dbS[(size_t) i] = (float) eqd::totalMagnitudeDb (bands, eqd::kMaxBands, eqd::Channel::Side, f, nullptr) + dbL[(size_t) i];
        const int bin = juce::jlimit (1, fftSize / 2 - 1, (int) std::lround (f / fs * (double) fftSize));
        specN[(size_t) i] = juce::jlimit (0.0f, 1.0f, (spectrum[(size_t) bin] + 90.0f) / 70.0f);
    }

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

    juce::RectangleList<float> bright, mid, faint, curveM, curveS;
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
            y = Hm * 0.5f - lookup (arr, p.u) / 18.0f * Hm * 0.42f + p.jy * (c == 0 ? 2.2f : 1.6f);   // grains, not a clean line
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
        g.drawText ("+18 dB", (int) W - 56, (int) (Hm * 0.08f) - 6, 50, 12, juce::Justification::right);
        g.drawText ("0 dB", (int) W - 56, (int) (Hm * 0.5f) - 6, 50, 12, juce::Justification::right);
        g.drawText ("-18 dB", (int) W - 56, (int) (Hm * 0.92f) - 6, 50, 12, juce::Justification::right);
    }

    // ---- phase strip (oscilloscope style): EQ phase response + stereo goniometer
    g.setColour (pal.ring);
    g.drawHorizontalLine ((int) Hm, 0.0f, W);
    const float cy = Hm + stripH * 0.5f, ph = stripH * 0.5f - 14.0f;
    const float gonioW = 170.0f, plotW = W - gonioW;
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
    const float gx = W - gonioW * 0.5f, gy = cy, gs = 46.0f;
    g.setColour (pal.ring);
    g.drawVerticalLine ((int) (W - gonioW), Hm, H);
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
    g.drawText ("CORR " + juce::String (corr >= 0.0f ? "+" : "") + juce::String (corr, 2), (int) (W - gonioW) + 4, (int) (H - 14), (int) gonioW - 8, 12,
                juce::Justification::centred);

    drawMeters (g);
}

void DuneField::drawMeters (juce::Graphics& g)
{
    const auto P = meterPanel;
    g.setColour (pal.bg.withAlpha (0.84f));
    g.fillRect (P);
    g.setColour (pal.ring);
    g.drawRect (P, 1.0f);

    auto fnum = [] (float v, int dp = 1) { return v <= -99.0f ? juce::String ("--") : juce::String (v, dp); };
    auto fdiff = [] (float a, float b)
    {
        if (a <= -99.0f || b <= -99.0f) return juce::String ("--");
        const float d = b - a; return (d > 0.05f ? juce::String ("+") : juce::String()) + juce::String (d, 1);
    };

    // ---------------- amber modulometer (arc window, two needles)
    const juce::Point<float> pivot (P.getX() + 135.0f, P.getY() + 182.0f);
    const float R = 150.0f, r = 106.0f, span = 46.0f;
    auto ang = [&] (float db) { return juce::degreesToRadians (-span + 2.0f * span * juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 43.0f)); };
    auto pt = [&] (float radius, float a) { return juce::Point<float> (pivot.x + radius * std::sin (a), pivot.y - radius * std::cos (a)); };

    juce::Path win;
    win.addCentredArc (pivot.x, pivot.y, R, R, 0.0f, juce::degreesToRadians (-span), juce::degreesToRadians (span), true);
    win.addCentredArc (pivot.x, pivot.y, r, r, 0.0f, juce::degreesToRadians (span), juce::degreesToRadians (-span), false);
    win.closeSubPath();
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (P.toNearestInt());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6b94a), pivot.x, pivot.y - R, juce::Colour (0xffd98a1c), pivot.x, pivot.y - r + 20.0f, false));
        g.fillPath (win);
        g.setColour (juce::Colour (0xff3a1a08));
        g.strokePath (win, juce::PathStrokeType (1.0f));

        // scale
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain)));
        for (float db : { -40.0f, -30.0f, -20.0f, -10.0f, -6.0f, -3.0f, 0.0f, 3.0f })
        {
            const float a = ang (db);
            g.setColour (db >= 0.0f ? juce::Colour (0xffa11a14) : juce::Colour (0xff3a1a08));
            g.drawLine ({ pt (R - 12.0f, a), pt (R - 2.0f, a) }, db == 0.0f ? 2.0f : 1.2f);
            const auto tp = pt (R - 22.0f, a);
            g.drawText (db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) db), (int) tp.x - 14, (int) tp.y - 6, 28, 12, juce::Justification::centred);
        }
        for (float db = -40.0f; db < 3.0f; db += 2.0f)
            g.drawLine ({ pt (R - 7.0f, ang (db)), pt (R - 2.0f, ang (db)) }, 0.7f);
        g.setColour (juce::Colour (0xff3a1a08));
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 10.5f, juce::Font::bold)));
        g.drawText ("MODULOMETER", (int) pivot.x - 60, (int) (pivot.y - R + 30.0f), 120, 14, juce::Justification::centred);
        g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 8.5f, juce::Font::plain)));
        g.drawText ("dBFS", (int) pt (R - 24.0f, ang (3.0f)).x - 12, (int) pt (R - 24.0f, ang (3.0f)).y + 8, 28, 10, juce::Justification::centred);

        // needles, clipped to the arc window like on the Nagra
        g.reduceClipRegion (win);
        const float ppm = std::max (proc.meters[0].ppm.load(), proc.meters[1].ppm.load());
        const float vu  = std::max (proc.meters[0].vu.load(),  proc.meters[1].vu.load());
        g.setColour (juce::Colour (0xff111111));
        g.drawLine ({ pt (r - 6.0f, ang (vu)), pt (R + 2.0f, ang (vu)) }, 1.6f);
        g.setColour (juce::Colour (0xffc0201a));
        g.drawLine ({ pt (r - 6.0f, ang (ppm)), pt (R + 2.0f, ang (ppm)) }, 1.6f);
    }
    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.5f, juce::Font::plain)));
    g.drawText (juce::String::fromUTF8 ("\xE2\x94\x80 rouge : PEAK    \xE2\x94\x80 noir : VU lent  (clic : reset)"), (int) P.getX() + 8, (int) P.getY() + 128, 270, 12,
                juce::Justification::left);

    // ---------------- readouts: input / output / difference
    const float tx = P.getX() + 292.0f;
    const float colX[4] { tx, tx + 80.0f, tx + 160.0f, tx + 240.0f };
    auto mono = juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
    auto small = juce::Font (juce::FontOptions ("Helvetica Neue", 9.5f, juce::Font::bold));
    float y = P.getY() + 6.0f;
    g.setColour (pal.dim);
    g.setFont (small);
    g.drawText ("MODULOMETRE", (int) tx, (int) y, 150, 12, juce::Justification::left);
    const double gainDb = proc.apvts.getRawParameterValue ("out")->load();
    g.drawText ("ENTREE", (int) colX[1], (int) y, 70, 12, juce::Justification::right);
    g.drawText ("SORTIE", (int) colX[2], (int) y, 70, 12, juce::Justification::right);
    g.drawText ("DIFF", (int) colX[3], (int) y, 70, 12, juce::Justification::right);

    const float outL = std::max (proc.meters[0].rms.load(), proc.meters[1].rms.load());
    const float outRms = 10.0f * std::log10 (0.5f * (std::pow (10.0f, proc.meters[0].rms.load() / 10.0f) + std::pow (10.0f, proc.meters[1].rms.load() / 10.0f)));
    juce::ignoreUnused (outL);
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
    y += 16.0f;
    for (const auto& rw : rows)
    {
        g.setColour (pal.dim);
        g.setFont (small);
        g.drawText (rw.name, (int) colX[0], (int) y, 80, 14, juce::Justification::left);
        g.setColour (pal.fg);
        g.setFont (mono);
        const bool isTp = std::strcmp (rw.name, "PEAK/TP") == 0;
        g.drawText (fnum (rw.in), (int) colX[1], (int) y, 70, 14, juce::Justification::right);
        g.setColour (isTp && rw.out > -1.0f ? juce::Colour (0xffe0463c) : pal.fg);
        g.drawText (fnum (rw.out), (int) colX[2], (int) y, 70, 14, juce::Justification::right);
        g.setColour (pal.fg);
        g.drawText (fdiff (rw.in, rw.out), (int) colX[3], (int) y, 70, 14, juce::Justification::right);
        y += 16.0f;
    }
    g.setColour (pal.dim);
    g.setFont (small);
    g.drawText ("GAIN SORTIE " + juce::String (gainDb >= 0.05 ? "+" : "") + juce::String (gainDb, 1) + " dB", (int) colX[0], (int) y, 150, 12, juce::Justification::left);
    g.drawText (proc.apvts.getRawParameterValue ("tp8x")->load() > 0.5f ? "TP 8x" : "TP 4x", (int) colX[3], (int) y, 70, 12, juce::Justification::right);

    // ---------------- dynamic-range spreader: loudness -> peak, input over output
    y += 16.0f;
    const float bx = colX[0] + 52.0f, bw = 270.0f;
    auto xOf = [&] (float db) { return bx + bw * juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 40.0f); };
    g.setColour (pal.ring);
    g.drawLine (bx, y + 28.0f, bx + bw, y + 28.0f, 1.0f);
    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 8.5f, juce::Font::plain)));
    for (int db = -40; db <= 0; db += 10)
    {
        g.drawLine (xOf ((float) db), y + 28.0f, xOf ((float) db), y + 31.0f, 1.0f);
        g.drawText (juce::String (db), (int) xOf ((float) db) - 12, (int) y + 31, 24, 10, juce::Justification::centred);
    }
    auto bar = [&] (float lo, float hi, float yy, const char* name, bool filled)
    {
        g.setColour (pal.dim);
        g.setFont (small);
        g.drawText (name, (int) colX[0], (int) yy - 2, 50, 12, juce::Justification::left);
        if (lo <= -99.0f || hi <= -99.0f) return;
        g.setColour (pal.fg);
        const float x0 = xOf (lo), x1 = xOf (hi);
        if (filled) g.fillRect (x0, yy, std::max (x1 - x0, 1.0f), 8.0f); else g.drawRect (x0, yy, std::max (x1 - x0, 1.0f), 8.0f, 1.0f);
        g.fillRect (x0 - 1.0f, yy - 2.0f, 2.0f, 12.0f);                // loudness
        g.fillRect (x1 - 1.0f, yy - 2.0f, 2.0f, 12.0f);                // peak
    };
    bar (inI, inPk, y + 2.0f, "ENTREE", false);
    bar (outI, outTp, y + 15.0f, "SORTIE", true);
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
    gainKnob.setVisible (hasGain);
    qKnob.setVisible (! isPass);

    const int ch = (int) b.ch;
    stBtn.setToggleState (ch == 0, juce::dontSendNotification);
    mBtn.setToggleState (ch == 3, juce::dontSendNotification);
    sBtn.setToggleState (ch == 4, juce::dontSendNotification);

    const float a = bandOn ? 1.0f : 0.38f;
    for (auto* c : std::initializer_list<juce::Component*> { &typeBox, &slopeBox, &freqKnob, &gainKnob, &qKnob, &stBtn, &mBtn, &sBtn })
        c->setAlpha (a);
    repaint();
}

void BandStrip::resized()
{
    numBtn.setBounds (8, 10, 30, 22);
    typeBox.setBounds (42, 10, 100, 22);
    freqKnob.setBounds (38, 40, 74, 74);
    slopeBox.setBounds (8, 167, 134, 22);
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
    if (! isPass)
    {
        g.setColour (pal.fg.withAlpha (a));
        g.drawText ("Q " + juce::String (qKnob.getValue(), 2), 73, 205, 70, 18, juce::Justification::centred);
    }
}

// ===========================================================================
DuneContent::DuneContent (MasteringEQAudioProcessor& p) : proc (p), field (p, palette), curvesBtn ("COURBES", palette), curvesTopBtn ("DESSUS", palette), tiltBtn ("TILT", palette), bypassBtn ("BYPASS", palette), lookBtn ("ANTICIPATION", palette), tp8Btn ("TP 8X", palette)
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
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (s);
    }
    tiltSliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "tilt_db", tiltSlider);
    outAtt        = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "out", outSlider);

    addAndMakeVisible (tiltBtn);
    addAndMakeVisible (bypassBtn);
    addAndMakeVisible (curvesBtn);
    addAndMakeVisible (curvesTopBtn);
    addAndMakeVisible (lookBtn);
    addAndMakeVisible (tp8Btn);
    lookAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "look", lookBtn);
    tp8Att  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "tp8x", tp8Btn);
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
    tp8Btn.setLabel (tp8Btn.getToggleState() ? "TP 8X" : "TP 4X");

    const double fs = std::max (proc.getSampleRate(), 1.0);
    footerText = juce::String::fromUTF8 ("64-bit  \xC2\xB7  courbes analogiques exactes  \xC2\xB7  latence ")
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
    tiltSlider.setBounds (834, y, 70, h);
    outSlider.setBounds (1022, y, 74, h);
    bypassBtn.setBounds (1148, y, 86, h);

    field.setBounds (0, 45, (int) kW, 418);
    for (auto* b : { &curvesBtn, &curvesTopBtn, &lookBtn, &tp8Btn }) b->toFront (false);
    lookBtn.setBounds (26, 45 + 10 + 164, 150, 26);
    tp8Btn.setBounds (186, 45 + 10 + 164, 110, 26);
    curvesBtn.setBounds (920, 52, 100, 26);
    curvesTopBtn.setBounds (1030, 52, 100, 26);
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
    g.drawText ((tilt > 0.05 ? "+" : "") + juce::String (tilt, 1), 908, 11, 48, 24, juce::Justification::centredLeft);
    g.drawText ("SORTIE", 950, 11, 66, 24, juce::Justification::centredRight);
    g.drawText (juce::String (paramValue (proc.apvts, "out"), 1), 1100, 11, 46, 24, juce::Justification::centredLeft);

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
