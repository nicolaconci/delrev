// Standalone unit tests for the DSP engine (no JUCE required).
// Build: g++ -std=c++17 -O2 -Wall tests/dsp_test.cpp Source/DSP/DelayHallEngine.cpp -o dsp_test
#include "DSP/DelayHallEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

using dsp::EngineParams;
using dsp::StereoDelayHallEngine;

static int failures = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (cond) { std::printf("PASS: %s\n", msg); }           \
        else { std::printf("FAIL: %s\n", msg); ++failures; }    \
    } while (0)

static int peakIndex(const std::vector<float>& v, int from, int to, float* peakVal = nullptr) {
    int best = from;
    float bv = std::fabs(v[(size_t) from]);
    for (int i = from + 1; i <= to && i < (int) v.size(); ++i) {
        if (std::fabs(v[(size_t) i]) > bv) { bv = std::fabs(v[(size_t) i]); best = i; }
    }
    if (peakVal) *peakVal = v[(size_t) best];
    return best;
}

static double sumAbs(const std::vector<float>& v, int from, int to) {
    double s = 0.0;
    for (int i = from; i <= to && i < (int) v.size(); ++i) s += std::fabs(v[(size_t) i]);
    return s;
}

static double rms(const std::vector<float>& v, int from, int to) {
    double s = 0.0;
    int n = 0;
    for (int i = from; i <= to && i < (int) v.size(); ++i) { s += (double) v[(size_t) i] * v[(size_t) i]; ++n; }
    return n ? std::sqrt(s / n) : 0.0;
}

int main() {
    const double sr = 44100.0;

    // 1) mix = 0 -> bit-transparent dry path, reverb must not touch the dry signal
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 0.0f; p.mixR = 0.0f; p.feedbackL = 0.9f; p.feedbackR = 0.9f; p.revMix = 1.0f; p.revDecaySec = 10.0f;
        p.timeLms = 100.0f; p.timeRms = 200.0f;
        e.setParams(p);
        std::vector<float> l(8192), r(8192);
        for (int i = 0; i < 8192; ++i) { l[i] = (i % 97 == 0) ? 1.0f : 0.001f * (i % 13); r[i] = -l[i]; }
        auto l0 = l, r0 = r;
        e.process(l.data(), r.data(), 8192);
        double maxd = 0.0;
        for (int i = 0; i < 8192; ++i)
            maxd = std::max(maxd, std::max((double) std::fabs(l[(size_t) i] - l0[(size_t) i]), (double) std::fabs(r[(size_t) i] - r0[(size_t) i])));
        CHECK(maxd < 1e-6, "mixL=mixR=0: dry passthrough exact (reverb not fed by dry)");
    }

    // 2) independent L/R repeat positions
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f; p.timeLms = 300.0f; p.timeRms = 450.0f;
        p.modDepthMs = 0.0f; p.revMix = 0.0f;
        e.setParams(p);
        std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
        l[0] = 1.0f; r[0] = 1.0f;
        e.process(l.data(), r.data(), 30000);
        const int expL = (int) (0.300 * sr); // 13230
        const int expR = (int) (0.450 * sr); // 19845
        float pvL = 0.0f, pvR = 0.0f;
        int pkL = peakIndex(l, expL - 20, expL + 20, &pvL);
        int pkR = peakIndex(r, expR - 20, expR + 20, &pvR);
        CHECK(std::abs(pkL - expL) <= 3 && pvL > 0.8f, "left repeat at 300 ms");
        CHECK(std::abs(pkR - expR) <= 3 && pvR > 0.8f, "right repeat at 450 ms");
        CHECK(sumAbs(l, 10, expL - 20) < 1e-3, "no energy before first left repeat");
    }

    // 3) feedback produces decaying repeats
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.5f; p.feedbackR = 0.5f; p.timeLms = 100.0f; p.timeRms = 100.0f;
        p.modDepthMs = 0.0f; p.revMix = 0.0f;
        e.setParams(p);
        std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
        l[0] = 1.0f;
        e.process(l.data(), r.data(), 20000);
        const int t = (int) (0.100 * sr); // 4410
        float v1 = 0, v2 = 0, v3 = 0;
        peakIndex(l, t - 20, t + 20, &v1);
        peakIndex(l, 2 * t - 20, 2 * t + 20, &v2);
        peakIndex(l, 3 * t - 20, 3 * t + 20, &v3);
        CHECK(std::fabs(v1 - 1.0f) < 0.05f, "1st repeat amplitude ~1.0");
        CHECK(std::fabs(v2 - 0.5f) < 0.08f, "2nd repeat amplitude ~0.5 (feedback)");
        CHECK(std::fabs(v3 - 0.25f) < 0.08f, "3rd repeat amplitude ~0.25");
    }

    // 4) reverb tail on repeats only
    {
        auto run = [&](float revMix) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f; p.timeLms = 100.0f; p.timeRms = 100.0f;
            p.modDepthMs = 0.0f; p.revMix = revMix; p.revDecaySec = 5.0f;
            e.setParams(p);
            std::vector<float> l(60000, 0.0f), r(60000, 0.0f);
            l[0] = 1.0f;
            e.process(l.data(), r.data(), 60000);
            return l;
        };
        const int t = (int) (0.100 * sr);
        auto withRev = run(1.0f);
        auto noRev = run(0.0f);
        CHECK(sumAbs(withRev, 10, t - 20) < 1e-3, "no reverb energy before the repeat");
        CHECK(rms(withRev, t + 100, t + 40000) > 1e-3, "reverb tail present after the repeat");
        CHECK(rms(noRev, t + 100, t + 40000) < 1e-4, "no tail when revMix = 0");
    }

    // 5) modulation shifts the repeat position within +/- depth (LFO on read pointer)
    {
        auto run = [&](float depthMs) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f; p.timeLms = 300.0f; p.timeRms = 300.0f;
            p.modRateHz = 1.0f; p.modDepthMs = depthMs; p.revMix = 0.0f;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            l[0] = 1.0f;
            e.process(l.data(), r.data(), 30000);
            return l;
        };
        const int expL = (int) (0.300 * sr); // 13230
        const int depthSamples = (int) (0.020 * sr); // 882
        float pvMod = 0.0f, pvNo = 0.0f;
        auto mod = run(20.0f);
        auto noMod = run(0.0f);
        int pkMod = peakIndex(mod, expL - depthSamples - 30, expL + depthSamples + 30, &pvMod);
        int pkNo = peakIndex(noMod, expL - 30, expL + 30, &pvNo);
        std::printf("  mod: peak at %d (base %d, depth +/-%d), no-mod peak at %d\n", pkMod, expL, depthSamples, pkNo);
        CHECK(std::abs(pkNo - expL) <= 3 && pvNo > 0.8f, "no modulation -> repeat at exact time");
        CHECK(std::abs(pkMod - expL) > 300 && std::abs(pkMod - expL) <= depthSamples + 10 && pvMod > 0.4f,
              "modulation moves repeat within +/- depth");
    }

    // 6) stability: noise at extreme settings, all finite and bounded
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.95f; p.feedbackR = 0.95f; p.timeLms = 50.0f; p.timeRms = 37.0f;
        p.modRateHz = 10.0f; p.modDepthMs = 30.0f; p.revMix = 1.0f; p.revDecaySec = 10.0f;
        p.grit = 1.0f; p.modVol = 0.7f; p.smear = 0.8f; p.swellMs = 100.0f;
        p.pitchL = 7.0f; p.pitchR = -12.0f; p.ratioL = 2.0f; p.ratioR = 3.0f;
        p.pitchMixL = 0.8f; p.pitchMixR = 0.8f;
        e.setParams(p);
        std::vector<float> l(44100), r(44100);
        unsigned int seed = 12345;
        float peak = 0.0f;
        bool finite = true;
        for (int i = 0; i < 44100; ++i) {
            seed = seed * 1664525u + 1013904223u;
            l[i] = ((seed >> 8) / 8388608.0f) - 1.0f;
            seed = seed * 1664525u + 1013904223u;
            r[i] = ((seed >> 8) / 8388608.0f) - 1.0f;
        }
        e.process(l.data(), r.data(), 44100);
        for (int i = 0; i < 44100; ++i) {
            finite = finite && std::isfinite(l[(size_t) i]) && std::isfinite(r[(size_t) i]);
            peak = std::max(peak, std::max(std::fabs(l[(size_t) i]), std::fabs(r[(size_t) i])));
        }
        std::printf("  extreme-settings noise peak: %f\n", (double) peak);
        CHECK(finite, "no NaN/Inf at extreme settings");
        CHECK(peak < 50.0f, "output bounded at extreme settings");
    }

    // 7) channel independence: impulse on L only -> R stays silent (no reverb crossfeed)
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.5f; p.feedbackR = 0.5f; p.timeLms = 100.0f; p.timeRms = 450.0f;
        p.revMix = 1.0f; p.revDecaySec = 5.0f; p.modDepthMs = 0.0f;
        e.setParams(p);
        std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
        l[0] = 1.0f;
        e.process(l.data(), r.data(), 30000);
        CHECK(sumAbs(r, 0, 29999) < 1e-5, "right channel unaffected by left-only input");
    }

    // 8) different sample rate (48 kHz)
    {
        StereoDelayHallEngine e;
        e.prepare(48000.0, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f; p.timeLms = 300.0f; p.timeRms = 300.0f;
        p.modDepthMs = 0.0f; p.revMix = 0.0f;
        e.setParams(p);
        std::vector<float> l(32000, 0.0f), r(32000, 0.0f);
        l[0] = 1.0f;
        e.process(l.data(), r.data(), 32000);
        const int exp = (int) (0.300 * 48000.0); // 14400
        float pv = 0.0f;
        int pk = peakIndex(l, exp - 20, exp + 20, &pv);
        CHECK(std::abs(pk - exp) <= 3 && pv > 0.8f, "repeat at 300 ms @ 48 kHz");
    }

    // 9) independent L/R mix: mixL=1, mixR=0 -> left is fully wet, right is exact dry
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 0.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f; p.timeLms = 300.0f; p.timeRms = 450.0f;
        p.modDepthMs = 0.0f; p.revMix = 0.0f;
        e.setParams(p);
        std::vector<float> l(30000), r(30000);
        for (int i = 0; i < 30000; ++i) {
            l[i] = (i % 61 == 0) ? 1.0f : 0.002f * (i % 7);
            r[i] = -l[i];
        }
        auto r0 = r;
        e.process(l.data(), r.data(), 30000);
        double maxdR = 0.0;
        for (int i = 0; i < 30000; ++i)
            maxdR = std::max(maxdR, (double) std::fabs(r[(size_t) i] - r0[(size_t) i]));
        const int expL = (int) (0.300 * sr);
        float pv = 0.0f;
        int pk = peakIndex(l, expL - 20, expL + 20, &pv);
        CHECK(maxdR < 1e-6, "mixR=0: right channel is exact dry passthrough");
        CHECK(std::abs(pk - expL) <= 3 && pv > 0.8f, "mixL=1: left channel carries the delay repeat");
    }

    // 10) pre gain and master volume (dB)
    {
        auto run = [&](float preDb, float masterDb, float mixL, float mixR) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.preGainDb = preDb; p.masterDb = masterDb;
            p.mixL = mixL; p.mixR = mixR; p.feedbackL = 0.0f; p.feedbackR = 0.0f; p.timeLms = 100.0f; p.timeRms = 100.0f;
            p.modDepthMs = 0.0f; p.revMix = 0.0f;
            e.setParams(p);
            std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
            l[0] = 1.0f;
            e.process(l.data(), r.data(), 20000);
            return l;
        };
        const int t = (int) (0.100 * sr);
        float pv = 0.0f;
        auto wet = run(6.0206f, 0.0f, 1.0f, 1.0f);
        peakIndex(wet, t - 20, t + 20, &pv);
        CHECK(std::fabs(pv - 2.0f) < 0.1f, "preGain +6 dB doubles the repeat");
        auto mastered = run(0.0f, -6.0206f, 1.0f, 1.0f);
        peakIndex(mastered, t - 20, t + 20, &pv);
        CHECK(std::fabs(pv - 0.5f) < 0.06f, "master -6 dB halves the output");
        auto dry = run(6.0206f, 0.0f, 0.0f, 0.0f);
        CHECK(std::fabs(dry[0] - 2.0f) < 1e-4f, "preGain also scales the dry path");
    }

    // 10b) wet gain applies to the wet path only
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
        p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
        p.wetGainDb = 6.0206f;
        e.setParams(p);
        std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
        l[0] = 1.0f;
        e.process(l.data(), r.data(), 20000);
        const int t = (int) (0.100 * sr);
        float pv = 0.0f;
        peakIndex(l, t - 20, t + 20, &pv);
        std::printf("  wetGain +6dB: repeat peak %.3f (expect ~2.0)\n", (double) pv);
        CHECK(std::fabs(pv - 2.0f) < 0.1f, "wetGain +6 dB doubles the wet repeat");
    }

    // 11) smear|crystal bipolar on the line input: negative = smear (lowpass), zero = off
    {
        auto run = [&](float smear) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.9f; p.feedbackR = 0.9f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.smear = smear;
            e.setParams(p);
            std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
            l[0] = 1.0f;
            e.process(l.data(), r.data(), 20000);
            return l;
        };
        const int t = (int) (0.100 * sr);
        float pv = 0.0f;
        auto clear = run(0.0f);
        peakIndex(clear, t - 20, t + 20, &pv);
        const float e1clear = std::fabs(pv);
        peakIndex(clear, 2 * t - 20, 2 * t + 20, &pv);
        const float e2clear = std::fabs(pv);
        auto smr = run(-1.0f);
        peakIndex(smr, t - 20, t + 20, &pv);
        const float e1smr = std::fabs(pv);
        peakIndex(smr, 2 * t - 20, 2 * t + 20, &pv);
        const float e2smr = std::fabs(pv);
        std::printf("  smear|crystal: echo1 clear %.3f smeared %.3f; echo2 clear %.3f smeared %.3f (ratio %.3f)\n",
                    (double) e1clear, (double) e1smr, (double) e2clear, (double) e2smr, (double) (e2smr / e1smr));
        CHECK(e1clear > 0.8f, "center (0): first repeat full");
        CHECK(e1smr < 0.5f * e1clear, "full CCW: first repeat attack softened (smear)");
        CHECK(std::fabs(e2smr / e1smr - 0.9f) < 0.1f, "no compounding: echo2/echo1 ratio stays = feedback");
    }

    // 12) crystal (smear > 0): blend toward highpass on the line input, capped at 2.5 kHz
    {
        auto run = [&](float freq, float smear) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.smear = smear;
            e.setParams(p);
            std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
            for (int i = 0; i < 20000; ++i)
                l[i] = std::sin(2.0 * 3.14159265 * freq * i / sr);
            e.process(l.data(), r.data(), 20000);
            return l;
        };
        const int t = (int) (0.100 * sr);
        float pv = 0.0f;
        auto hiClear = run(8000.0f, 0.0f);
        peakIndex(hiClear, t + 500, t + 3000, &pv);
        const float a8c = std::fabs(pv);
        auto hiX = run(8000.0f, 1.0f);
        peakIndex(hiX, t + 500, t + 3000, &pv);
        const float a8x = std::fabs(pv);
        auto loClear = run(500.0f, 0.0f);
        peakIndex(loClear, t + 500, t + 3000, &pv);
        const float a5c = std::fabs(pv);
        auto loX = run(500.0f, 1.0f);
        peakIndex(loX, t + 500, t + 3000, &pv);
        const float a5x = std::fabs(pv);
        std::printf("  crystal @+100%%: 8kHz %.3f (clear %.3f), 500Hz %.3f (clear %.3f)\n",
                    (double) a8x, (double) a8c, (double) a5x, (double) a5c);
        CHECK(a8x > 0.7f * a8c, "crystal keeps 8 kHz (bright shimmer)");
        CHECK(a5x < 0.85f * a5c, "crystal still cuts 500 Hz somewhat (highpass)");
        CHECK(a5x > 0.25f * a5c, "crystal gain compensation: repeat not lost when crystal is up");
        CHECK(a5x > 0.03f, "crystal at 100% does NOT silence the repeat");

        // crystal adds a sharp attack at the start of the repeat ("glass break")
        auto runImp = [&](float smear) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.smear = smear;
            e.setParams(p);
            std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
            l[0] = 1.0f;
            e.process(l.data(), r.data(), 20000);
            return l;
        };
        float pv2 = 0.0f;
        auto impClear = runImp(0.0f);
        peakIndex(impClear, t - 20, t + 20, &pv2);
        const float impC = std::fabs(pv2);
        auto impX = runImp(1.0f);
        peakIndex(impX, t - 20, t + 20, &pv2);
        const float impXAmp = std::fabs(pv2);
        std::printf("  crystal attack: impulse echo clear %.3f crystal %.3f\n", (double) impC, (double) impXAmp);
        CHECK(impXAmp > 1.3f * impC, "crystal: sharper attack at the start of the repeat");
    }

    // 13) mono-to-stereo behavior at engine level: same input on both channels,
    //     independent L/R times -> L and R outputs carry repeats at different times
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
        p.timeLms = 300.0f; p.timeRms = 450.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
        e.setParams(p);
        std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
        l[0] = 1.0f;
        std::copy(l.begin(), l.end(), r.begin()); // mono input duplicated
        e.process(l.data(), r.data(), 30000);
        float pvL = 0.0f, pvR = 0.0f;
        int pkL = peakIndex(l, (int) (0.300 * sr) - 20, (int) (0.300 * sr) + 20, &pvL);
        int pkR = peakIndex(r, (int) (0.450 * sr) - 20, (int) (0.450 * sr) + 20, &pvR);
        CHECK(std::abs(pkL - (int) (0.300 * sr)) <= 3 && pvL > 0.8f, "mono->stereo: left repeat at 300 ms");
        CHECK(std::abs(pkR - (int) (0.450 * sr)) <= 3 && pvR > 0.8f, "mono->stereo: right repeat at 450 ms");
    }

    // 14) tone L/R: per-channel lowpass on the wet (bright vs dark delay)
    {
        auto run = [&](float freq, float tone) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.toneL = tone; p.toneR = tone;
            e.setParams(p);
            std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
            for (int i = 0; i < 20000; ++i)
                l[i] = std::sin(2.0 * 3.14159265 * freq * i / sr);
            e.process(l.data(), r.data(), 20000);
            return l;
        };
        const int t = (int) (0.100 * sr);
        float pv = 0.0f;
        auto bright8k = run(8000.0f, 1.0f);
        peakIndex(bright8k, t + 500, t + 3000, &pv);
        const float b8 = std::fabs(pv);
        auto dark8k = run(8000.0f, 0.0f);
        peakIndex(dark8k, t + 500, t + 3000, &pv);
        const float d8 = std::fabs(pv);
        auto dark200 = run(200.0f, 0.0f);
        peakIndex(dark200, t + 500, t + 3000, &pv);
        const float dLow = std::fabs(pv);
        std::printf("  tone: 8kHz bright %.3f dark %.3f; 200Hz dark %.3f\n",
                    (double) b8, (double) d8, (double) dLow);
        CHECK(b8 > 0.8f, "tone=100%: 8 kHz echo passes (bright)");
        CHECK(d8 < 0.15f * b8, "tone=0%: 8 kHz echo heavily attenuated (dark)");
        CHECK(dLow > 0.7f, "tone=0%: 200 Hz still passes (lowpass, not highpass)");
    }

    // 15) swell: volume swell-in per repeat on the wet; toggle applies it to the dry input too
    {
        auto run = [&](float swellMs, bool onInput, float mix) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = mix; p.mixR = mix; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.swellMs = swellMs; p.swellOnInput = onInput;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            for (int i = 1000; i < 9000; ++i) l[i] = 1.0f; // burst after silence
            e.process(l.data(), r.data(), 30000);
            return l;
        };
        const int t = (int) (0.100 * sr);
        const int S = (int) (0.100 * sr); // swell = 100 ms
        const int burst = 1000;
        float pv = 0.0f;
        auto wet = run(100.0f, false, 1.0f);
        peakIndex(wet, t + burst + 400, t + burst + S / 2, &pv);
        const float wetEarly = std::fabs(pv);
        peakIndex(wet, t + burst + 6500, t + burst + 7500, &pv);
        const float wetLate = std::fabs(pv);
        auto dryOff = run(100.0f, false, 0.0f);
        peakIndex(dryOff, burst + 400, burst + S / 2, &pv);
        const float dryOffEarly = std::fabs(pv);
        auto dryOn = run(100.0f, true, 0.0f);
        peakIndex(dryOn, burst + 400, burst + S / 2, &pv);
        const float dryOnEarly = std::fabs(pv);
        peakIndex(dryOn, burst + 6500, burst + 7500, &pv);
        const float dryOnLate = std::fabs(pv);
        std::printf("  swell: wet early %.3f late %.3f | dry off %.3f, on early %.3f late %.3f\n",
                    (double) wetEarly, (double) wetLate, (double) dryOffEarly, (double) dryOnEarly, (double) dryOnLate);
        CHECK(wetEarly < 0.8f, "swell: repeat swells in (early part quiet)");
        CHECK(wetLate > 0.85f, "swell: repeat reaches full level after the swell");
        CHECK(dryOffEarly > 0.9f, "toggle off: dry input NOT swelled");
        CHECK(dryOnEarly < 0.8f && dryOnLate > 0.85f, "toggle on: dry input swells too");

        // level-independent: a quiet burst (amp 0.05) still triggers the swell
        {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.swellMs = 100.0f; p.swellOnInput = false;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            for (int i = 1000; i < 9000; ++i) l[i] = 0.05f; // quiet burst
            e.process(l.data(), r.data(), 30000);
            float pv2 = 0.0f;
            peakIndex(l, t + burst + 400, t + burst + S / 2, &pv2);
            const float qEarly = std::fabs(pv2);
            peakIndex(l, t + burst + 6500, t + burst + 7500, &pv2);
            const float qLate = std::fabs(pv2);
            std::printf("  swell quiet burst: early %.4f late %.4f\n", (double) qEarly, (double) qLate);
            CHECK(qEarly < 0.8f * qLate && qLate > 0.04f, "swell triggers also on quiet transients (level-independent)");
        }

        // two bursts 300 ms apart (continuous playing): the second onset must also swell
        {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.swellMs = 100.0f; p.swellOnInput = false;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            for (int i = 1000; i < 3000; ++i) l[i] = 1.0f;
            for (int i = 4300; i < 11000; ++i) l[i] = 1.0f; // second burst, longer so the swell completes
            e.process(l.data(), r.data(), 30000);
            float pv2 = 0.0f;
            // echo of the second burst starts at t+4300
            peakIndex(l, t + 5200, t + 5800, &pv2);
            const float b2Early = std::fabs(pv2);
            peakIndex(l, t + 9700, t + 10900, &pv2);
            const float b2Late = std::fabs(pv2);
            std::printf("  swell 2nd burst: early %.3f late %.3f\n", (double) b2Early, (double) b2Late);
            CHECK(b2Early < 0.8f * b2Late, "swell fires again on the next note (continuous playing)");
        }
    }

    // 16) reverb width (mid/side): 0% = mono tail (L==R), 200% = extra wide (L!=R)
    {
        auto run = [&](float width) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f;
            p.revMix = 1.0f; p.revDecaySec = 5.0f; p.revWidth = width;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            l[0] = 1.0f; // impulse on L only
            e.process(l.data(), r.data(), 30000);
            return std::make_pair(l, r);
        };
        const int t = (int) (0.100 * sr);
        auto mono = run(0.0f);
        auto wide = run(2.0f);
        double maxDiffMono = 0.0, maxDiffWide = 0.0;
        for (int i = t + 100; i < 30000; ++i) {
            maxDiffMono = std::max(maxDiffMono, (double) std::fabs(mono.first[(size_t) i] - mono.second[(size_t) i]));
            maxDiffWide = std::max(maxDiffWide, (double) std::fabs(wide.first[(size_t) i] - wide.second[(size_t) i]));
        }
        std::printf("  rev width: max|L-R| at 0%% = %.5f, at 200%% = %.5f\n", maxDiffMono, maxDiffWide);
        CHECK(maxDiffMono < 1e-3, "width 0%: reverb tail is mono (L == R)");
        CHECK(maxDiffWide > 1e-3, "width 200%: reverb tail is wide (L != R)");
    }

    // 17) grit: soft-clip on the repeats, constant volume (RMS make-up), hot peaks clamped but not louder
    {
        auto runImp = [&](float grit) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.preGainDb = 12.0f; p.grit = grit;
            e.setParams(p);
            std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
            l[0] = 1.0f;
            e.process(l.data(), r.data(), 20000);
            return l;
        };
        auto runSine = [&](float grit) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.grit = grit;
            e.setParams(p);
            std::vector<float> l(20000, 0.0f), r(20000, 0.0f);
            for (int i = 0; i < 20000; ++i)
                l[i] = 0.5f * std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
            e.process(l.data(), r.data(), 20000);
            return l;
        };
        const int t = (int) (0.100 * sr);
        float pv = 0.0f;
        auto clean = runImp(0.0f);
        peakIndex(clean, t - 20, t + 20, &pv);
        const float c = pv; // ~4 (preGain +12 dB)
        auto gritty = runImp(1.0f);
        peakIndex(gritty, t - 20, t + 20, &pv);
        std::printf("  grit: echo peak clean %.2f gritty %.2f\n", (double) c, (double) pv);
        CHECK(c > 3.5f, "grit=0: hot repeat passes (~4)");
        CHECK(pv < 0.6f * c, "grit=1: hot repeat soft-clipped, NOT louder than clean");
        // constant volume: sustained sine, RMS of the repeat with grit ~ without grit
        auto s0 = runSine(0.0f);
        auto s1 = runSine(1.0f);
        const double r0 = rms(s0, t + 2000, t + 8000);
        const double r1 = rms(s1, t + 2000, t + 8000);
        float pk0 = 0.0f, pk1 = 0.0f;
        peakIndex(s0, t + 2000, t + 8000, &pk0);
        peakIndex(s1, t + 2000, t + 8000, &pk1);
        const double crest0 = r0 > 1e-9 ? std::fabs(pk0) / r0 : 0.0;
        const double crest1 = r1 > 1e-9 ? std::fabs(pk1) / r1 : 0.0;
        std::printf("  grit: sine rms clean %.4f gritty %.4f (ratio %.3f), crest %.3f -> %.3f\n",
                    r0, r1, r0 > 1e-9 ? r1 / r0 : 0.0, crest0, crest1);
        CHECK(r1 > 0.8 * r0 && r1 < 1.25 * r0, "grit=1: repeat RMS ~constant (volume unchanged)");
        CHECK(crest1 < crest0 - 0.1, "grit=1: crest factor drops (signal actually saturated)");
    }

    // 18) modVol: LFO depth scaled by mod volume (0 = no modulation)
    {
        auto run = [&](float modVol) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 300.0f; p.timeRms = 300.0f; p.modRateHz = 1.0f; p.modDepthMs = 20.0f;
            p.modVol = modVol; p.revMix = 0.0f;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            l[0] = 1.0f;
            e.process(l.data(), r.data(), 30000);
            return l;
        };
        const int expL = (int) (0.300 * sr);
        auto full = run(1.0f);
        auto off = run(0.0f);
        float pv = 0.0f;
        int pkFull = peakIndex(full, expL - 1000, expL + 1000, &pv);
        int pkOff = peakIndex(off, expL - 30, expL + 30, &pv);
        std::printf("  modVol: peak pos full %d, off %d (base %d)\n", pkFull, pkOff, expL);
        CHECK(std::abs(pkFull - expL) > 300, "modVol=1: repeat moves with LFO");
        CHECK(std::abs(pkOff - expL) <= 3, "modVol=0: no modulation");
    }

    // 19) pitch in semitones: +12 st reads at 2x -> octave UP; -12 st -> octave DOWN; 0 st = bypass
    {
        auto run = [&](float pitchSt) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.pitchL = pitchSt;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            for (int i = 0; i < 30000; ++i)
                l[i] = std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
            e.process(l.data(), r.data(), 30000);
            return l;
        };
        auto countZC = [&](const std::vector<float>& v, int from, int to) {
            int c = 0;
            for (int i = from + 1; i <= to && i < (int) v.size(); ++i)
                if ((v[(size_t) i] >= 0) != (v[(size_t) (i - 1)] >= 0)) ++c;
            return c;
        };
        const int t = (int) (0.100 * sr);
        auto base = run(0.0f);   // 0 st = bypass
        auto up = run(12.0f);    // +12 st -> read rate 2 -> octave up
        auto down = run(-12.0f); // -12 st -> read rate 0.5 -> octave down
        // difference = only the extra pitch-shifted line (main echo identical in both runs)
        std::vector<float> diffUp(30000, 0.0f), diffDown(30000, 0.0f);
        for (int i = 0; i < 30000; ++i) {
            diffUp[(size_t) i] = up[(size_t) i] - base[(size_t) i];
            diffDown[(size_t) i] = down[(size_t) i] - base[(size_t) i];
        }
        const int zc0 = countZC(base, t + 2500, t + 20000);
        const int zcUp = countZC(diffUp, t + 2500, t + 20000);
        const int zcDown = countZC(diffDown, t + 2500, t + 20000);
        std::printf("  pitch st: zc main %d, +12st %d, -12st %d\n", zc0, zcUp, zcDown);
        CHECK(zcUp > (int) (1.3 * zc0), "pitch +12 st: extra line shifted up (~octave, loop crossfade smears the count)");
        CHECK(zcDown > 0 && zcDown < (int) (0.75 * zc0), "pitch -12 st: extra line shifted down (~octave down)");
        // pitchMix = 0 -> extra line fully muted
        {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.pitchL = 12.0f; p.pitchMixL = 0.0f;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            for (int i = 0; i < 30000; ++i) l[i] = std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
            e.process(l.data(), r.data(), 30000);
            double en = 0.0;
            for (int i = t + 2500; i < t + 20000; ++i) en += std::fabs(l[(size_t) i] - base[(size_t) i]);
            std::printf("  pitchMix=0: extra-line energy %.5f\n", en);
            CHECK(en < 1e-3, "pitchMix=0: extra line muted");
        }
    }

    // 20) ratio: pitch line time = ratio x main time (pitch active, ratio 2:1 -> echo around 200 ms)
    {
        auto run = [&](float pitchSt, float ratio) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.pitchL = pitchSt; p.ratioL = ratio; p.pitchMixL = 1.0f;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            for (int i = 1000; i < 9000; ++i) // burst 22..204 ms
                l[i] = std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
            e.process(l.data(), r.data(), 30000);
            return l;
        };
        auto on = run(12.0f, 2.0f);   // ratio 2:1 -> pitch line around 200 ms
        auto off = run(0.0f, 1.0f);   // pitch 0 = bypass -> no extra line
        std::vector<float> diff(30000, 0.0f);
        for (int i = 0; i < 30000; ++i) diff[(size_t) i] = on[(size_t) i] - off[(size_t) i];
        const int t = (int) (0.100 * sr);   // main echo at 100 ms
        const int t2 = (int) (0.200 * sr);  // pitch line at 2 x 100 ms
        const double eMain = sumAbs(on, t - 500, t + 1500);        // main echo of the burst at 100 ms
        const double eEarly = sumAbs(diff, t + 1500, t2 - 1000);    // between main echo and 200 ms
        const double ePitch = sumAbs(diff, t2 - 100, t2 + 6000);    // around 200 ms
        std::printf("  pitch ratio time: main %.1f, diff before 200ms %.3f, around 200ms %.1f\n", eMain, eEarly, ePitch);
        CHECK(eMain > 100.0, "main echo of the burst at 100 ms");
        CHECK(eEarly < 0.05 * ePitch, "ratio 2:1: no pitch-line energy before ~2x main time");
        CHECK(ePitch > 100.0, "ratio 2:1: pitch line echoes around 2x the main time (200 ms)");
        // ratio snaps to the 4 discrete steps: 1.7 -> 2:1
        auto snapped = run(12.0f, 1.7f);
        std::vector<float> diffSnap(30000);
        for (int i = 0; i < 30000; ++i) diffSnap[(size_t) i] = snapped[(size_t) i] - off[(size_t) i];
        const double eSnap = sumAbs(diffSnap, t2 - 100, t2 + 6000);
        std::printf("  ratio snap: 1.7 snapped to 2:1, energy around 200ms %.1f\n", eSnap);
        CHECK(eSnap > 100.0, "ratio 1.7 snaps to 2:1 (echo at 200 ms)");
    }

    // 21) pitch mix is independent of Mix L: at mix=30% the pitch line still comes through at pitchMix level
    {
        auto run = [&](float pitchMix) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 0.3f; p.mixR = 0.3f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
            p.pitchL = 12.0f; p.ratioL = 2.0f; p.pitchMixL = pitchMix;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            for (int i = 0; i < 30000; ++i)
                l[i] = std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
            e.process(l.data(), r.data(), 30000);
            return l;
        };
        const int t = (int) (0.100 * sr);
        auto off = run(0.0f);
        auto on = run(1.0f);
        // difference isolates the pitch line (dry + main echo identical in both runs)
        std::vector<float> diff(30000, 0.0f);
        for (int i = 0; i < 30000; ++i) diff[(size_t) i] = on[(size_t) i] - off[(size_t) i];
        const double eDiff = rms(diff, t + 500, t + 20000);
        const double eMain = rms(on, t + 500, t + 20000);
        std::printf("  pitchMix independence: pitch-line rms %.4f vs output rms %.4f (mix 30%%)\n", eDiff, eMain);
        CHECK(eDiff > 0.3 * eMain, "pitch line level independent of Mix L (audible at mix 30% with pitchMix 100%)");
    }

    // 22) capture-hold: the pitch line is silent before the first transient, then the
    //     captured snippet is transposed, held (looped) and echoed; it fades out after
    {
        StereoDelayHallEngine e;
        e.prepare(sr, 512);
        EngineParams p;
        p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
        p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f; p.revMix = 0.0f;
        p.pitchL = 12.0f; p.ratioL = 2.0f; p.pitchMixL = 1.0f;
        e.setParams(p);
        std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
        for (int i = 1000; i < 3000; ++i) // short burst 22..68 ms
            l[i] = std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
        e.process(l.data(), r.data(), 30000);
        const int t = (int) (0.100 * sr);
        const double ePre = sumAbs(l, 500, t + 900);        // before any echo (burst starts at 1000)
        const double eHold = sumAbs(l, t + 2000, t + 14000); // after the burst: held loop + echo (ratio 2:1, fuzzy loop)
        const double eLate = sumAbs(l, 25000, 29000);      // long after: gate released
        std::printf("  capture-hold: pre %.5f, hold %.2f, late %.5f\n", ePre, eHold, eLate);
        CHECK(ePre < 1e-3, "capture-hold: silent before the first transient");
        CHECK(eHold > 1.0, "capture-hold: captured snippet transposed+held after the transient");
        CHECK(eLate < 1e-3, "capture-hold: fades out long after (gate release)");
    }

    // 23) Pitch Mix gates the pitch line completely: at pitchMix=0 the output is identical to
    //     pitch bypass (no pitch dry AND no pitch in the reverb tail); at pitchMix>0 the pitch
    //     echo is present post-mix, but it does NOT feed the shared reverb
    {
        auto run = [&](float pitchSt, float pitchMix, float revMix) {
            StereoDelayHallEngine e;
            e.prepare(sr, 512);
            EngineParams p;
            p.mixL = 1.0f; p.mixR = 1.0f; p.feedbackL = 0.0f; p.feedbackR = 0.0f;
            p.timeLms = 100.0f; p.timeRms = 100.0f; p.modDepthMs = 0.0f;
            p.revMix = revMix; p.revDecaySec = 5.0f;
            p.pitchL = pitchSt; p.ratioL = 2.0f; p.pitchMixL = pitchMix;
            e.setParams(p);
            std::vector<float> l(30000, 0.0f), r(30000, 0.0f);
            for (int i = 1000; i < 3000; ++i)
                l[i] = std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
            e.process(l.data(), r.data(), 30000);
            return l;
        };
        auto bypass = run(0.0f, 0.0f, 1.0f);  // pitch 0 = bypass, reverb on
        auto muted  = run(12.0f, 0.0f, 1.0f); // pitch active but Pitch Mix 0
        auto on     = run(12.0f, 1.0f, 1.0f); // pitch active, Pitch Mix 100%
        double maxd = 0.0;
        for (int i = 0; i < 30000; ++i)
            maxd = std::max(maxd, (double) std::fabs(muted[(size_t) i] - bypass[(size_t) i]));
        std::vector<float> diff(30000);
        for (int i = 0; i < 30000; ++i) diff[(size_t) i] = on[(size_t) i] - bypass[(size_t) i];
        const int t = (int) (0.100 * sr);
        const double eEcho = sumAbs(diff, t + 1000, t + 14000); // pitch echo window (ratio 2:1, fuzzy loop)
        const double eTail = sumAbs(diff, 18000, 26000);        // far tail: reverb only
        std::printf("  pitchMix gate: max|muted-bypass| %.6f, echo %.2f, pitch tail %.3f\n", maxd, eEcho, eTail);
        CHECK(maxd < 1e-6, "pitchMix=0: output identical to bypass (pitch fully silenced, gate after its reverb)");
        CHECK(eEcho > 10.0, "pitchMix=100%: pitch echo present post-mix");
        CHECK(eTail > 0.05, "pitch line has its own hall tail (delay -> rev -> pitchMix), gated by Pitch Mix");
    }

    std::printf(failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
