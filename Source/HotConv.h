#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <mutex>

// Click-free convolution whose impulse response can be replaced while audio runs.
// juce::dsp::Convolution swaps its engine with an empty history, so a long (linear-phase) IR
// drops out for its whole length on every change. Here the new IR is loaded into a second
// convolver that is fed the same input until it holds a full IR length of history, and only
// then is it cross-faded in.
class HotConv
{
public:
    using Stereo = juce::dsp::Convolution::Stereo;
    using Trim = juce::dsp::Convolution::Trim;
    using Normalise = juce::dsp::Convolution::Normalise;

    void prepare (const juce::dsp::ProcessSpec& s)
    {
        for (auto& c : cv) { c.prepare (s); c.reset(); }
        tmp.setSize (1, (int) s.maximumBlockSize, false, false, true);
        std::lock_guard<std::mutex> g (mtx);
        sr = s.sampleRate;
        hasIR = false; pending = false; fadeLeft = 0; warm = 0; active = 0;
    }

    void reset()
    {
        for (auto& c : cv) c.reset();
        fadeLeft = 0; warm = 0;
    }

    void loadImpulseResponse (juce::AudioBuffer<float>&& b, double fs, Stereo s, Trim t, Normalise n)
    {
        const int len = b.getNumSamples();
        for (int i = 0; i < 100 && fadeLeft.load() > 0; ++i) juce::Thread::sleep (2);   // let a running fade finish
        std::lock_guard<std::mutex> g (mtx);
        const bool idle = ! hasIR || fadeLeft.load() > 0 || (juce::Time::getMillisecondCounter() - lastProc.load()) > 250u;
        if (idle)                                       // nothing is playing: replace directly
        {
            cv[(size_t) active.load()].loadImpulseResponse (std::move (b), fs, s, t, n);
            hasIR = true; pending = false;
            return;
        }
        cv[(size_t) (1 - active.load())].loadImpulseResponse (std::move (b), fs, s, t, n);
        warm = 0;
        target = len + (int) (0.5 * sr);                // full IR of history + time for the engine to be built
        pending = true;
    }

    void process (const juce::dsp::ProcessContextReplacing<float>& ctx)
    {
        auto out = ctx.getOutputBlock();
        const int n = (int) out.getNumSamples();
        lastProc = juce::Time::getMillisecondCounter();

        if (pending.load() && fadeLeft.load() == 0 && warm.load() >= target.load() && mtx.try_lock())
        {
            if (pending.load() && warm.load() >= target.load())
            {
                active = 1 - active.load();
                pending = false;
                fadeLen = std::max (1024, (int) (0.03 * sr));
                fadeLeft = fadeLen;
            }
            mtx.unlock();
        }

        const int a = active.load();
        const bool other = pending.load() || fadeLeft.load() > 0;
        if (other)
        {
            std::copy (out.getChannelPointer (0), out.getChannelPointer (0) + n, tmp.getWritePointer (0));
            juce::dsp::AudioBlock<float> tb (tmp);
            auto sub = tb.getSubBlock (0, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> c2 (sub);
            cv[(size_t) (1 - a)].process (c2);
        }
        cv[(size_t) a].process (ctx);

        int fl = fadeLeft.load();
        if (fl > 0)
        {
            float* o = out.getChannelPointer (0);
            const float* t = tmp.getReadPointer (0);
            for (int i = 0; i < n && fl > 0; ++i, --fl)
            {
                const float w = 1.0f - (float) fl / (float) fadeLen;
                o[i] = o[i] * w + t[i] * (1.0f - w);
            }
            fadeLeft = fl;
        }
        else if (pending.load())
            warm += n;
    }

private:
    juce::dsp::Convolution cv[2];
    juce::AudioBuffer<float> tmp;
    std::mutex mtx;
    std::atomic<int> active { 0 }, fadeLeft { 0 }, warm { 0 }, target { 0 };
    std::atomic<bool> pending { false };
    std::atomic<juce::uint32> lastProc { 0 };
    int fadeLen = 1024;
    double sr = 44100.0;
    bool hasIR = false;
};
