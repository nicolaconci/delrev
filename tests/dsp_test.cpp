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

    // 11) smear lowpasses the feedback path: later repeats get a softer attack
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
        auto noSmear = run(0.0f);
        peakIndex(noSmear, 2 * t - 20, 2 * t + 20, &pv);
        const float ref = pv;
        auto smeared = run(1.0f);
        peakIndex(smeared, 2 * t - 20, 2 * t + 20, &pv);
        std::printf("  smear: 2nd repeat amp %.3f (no smear) vs %.3f (smear=1)\n", (double) ref, (double) pv);
        CHECK(ref > 0.8f, "no smear: 2nd repeat strong");
        CHECK(pv < 0.2f * ref, "smear=1: 2nd repeat heavily softened");
        // first repeat must stay unfiltered even with smear = 1
        peakIndex(smeared, t - 20, t + 20, &pv);
        CHECK(pv > 0.8f, "smear does not touch the first repeat");
    }

    std::printf(failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
