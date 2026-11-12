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
    meterPanel = { 14.0f, 10.0f, 530.0f, 72.0f };
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
    g.setColour (pal.bg.withAlpha (0.78f));
    g.fillRect (meterPanel);
    g.setColour (pal.ring);
    g.drawRect (meterPanel, 1.0f);

    g.setColour (pal.dim);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.5f, juce::Font::bold)));
    g.drawText ("MODULOMETRE   VU / PPM / RMS / TRUE PEAK   (clic : reset crete)", (int) meterPanel.getX() + 10, (int) meterPanel.getY() + 5, 500, 12,
                juce::Justification::left);

    auto mono = juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
    auto num = [] (float v) { return v <= -100.0f ? juce::String ("  --") : juce::String (v, 1); };
    const float bx = meterPanel.getX() + 28.0f, bw = 170.0f;
    auto vuToX = [&] (float vu) { return bx + bw * juce::jlimit (0.0f, 1.0f, (vu + 20.0f) / 23.0f); };

    for (int ch = 0; ch < 2; ++ch)
    {
        const float y = meterPanel.getY() + 22.0f + (float) ch * 22.0f;
        const float vu = proc.meters[ch].vu.load(), ppm = proc.meters[ch].ppm.load();
        const float rms = proc.meters[ch].rms.load(), tp = proc.meters[ch].truePeak.load();

        g.setColour (pal.dim);
        g.setFont (mono);
        g.drawText (ch == 0 ? "L" : "R", (int) meterPanel.getX() + 10, (int) y, 14, 14, juce::Justification::centred);
        g.setColour (pal.ring);
        g.drawRect (bx, y + 2.0f, bw, 10.0f, 1.0f);
        g.setColour (pal.fg);
        g.fillRect (bx, y + 2.0f, vuToX (vu + 18.0f) - bx, 10.0f);              // VU bar (0 VU = -18 dBFS)
        g.setColour (pal.dim);
        g.drawLine (vuToX (0.0f), y, vuToX (0.0f), y + 14.0f, 1.0f);              // 0 VU mark
        g.setColour (pal.fg);
        g.fillRect (vuToX (ppm + 18.0f) - 1.0f, y, 2.0f, 14.0f);                 // PPM marker

        g.setColour (pal.fg);
        g.setFont (mono);
        g.drawText ("VU " + num (vu + 18.0f) + "  RMS " + num (rms) + "  PPM " + num (ppm) + "  TP " + num (tp) + (tp > -1.0f ? " !" : ""),
                    (int) (bx + bw + 12.0f), (int) y, 300, 14, juce::Justification::left);
    }
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
DuneContent::DuneContent (MasteringEQAudioProcessor& p) : proc (p), field (p, palette)
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
    curvesTopBtn.setButtonText (curvesTopBtn.getToggleState() ? "DESSUS" : "DESSOUS");
    curvesTopBtn.setEnabled (curvesBtn.getToggleState());

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
    tiltBtn.setBounds (776, y, 52, h);
    tiltSlider.setBounds (834, y, 70, h);
    outSlider.setBounds (1022, y, 74, h);
    bypassBtn.setBounds (1152, y, 76, h);

    field.setBounds (0, 45, (int) kW, 418);
    curvesBtn.toFront (false); curvesTopBtn.toFront (false);
    curvesBtn.setBounds (930, 53, 92, 22);
    curvesTopBtn.setBounds (1028, 53, 92, 22);
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
