#include "DelayHallEngine.h"

#include <algorithm>

namespace dsp {

void StereoDelayHallEngine::prepare(double sr, int /*maxBlockSize*/) {
    sampleRate = sr;
    smoothCoef = 1.0f - std::exp(-1.0f / (float) (sr * 0.005)); // ~5 ms smoothing
    const int maxSamples = (int) std::ceil(maxTimeMs * 0.001 * sr)
                         + (int) std::ceil(maxModDepthMs * 0.001 * sr) + 8;
    lineL.resize(maxSamples);
    lineR.resize(maxSamples);
    reverb.prepare(sr);
    EngineParams def;
    target = def;
    mixL = def.mixL;
    mixR = def.mixR;
    feedback = def.feedback;
    timeL = def.timeLms;
    timeR = def.timeRms;
    modRate = def.modRateHz;
    modDepth = def.modDepthMs;
    revDecaySec = def.revDecaySec;
    revMix = def.revMix;
    paramsSnapped = false;
    lfoPhase = 0.0f;
}

void StereoDelayHallEngine::setParams(const EngineParams& p) {
    EngineParams c = p;
    c.mixL = std::clamp(c.mixL, 0.0f, 1.0f);
    c.mixR = std::clamp(c.mixR, 0.0f, 1.0f);
    c.feedback = std::clamp(c.feedback, 0.0f, 0.95f);
    c.timeLms = std::clamp(c.timeLms, minTimeMs, maxTimeMs);
    c.timeRms = std::clamp(c.timeRms, minTimeMs, maxTimeMs);
    c.modRateHz = std::clamp(c.modRateHz, 0.01f, 20.0f);
    c.modDepthMs = std::clamp(c.modDepthMs, 0.0f, maxModDepthMs);
    c.revDecaySec = std::clamp(c.revDecaySec, 0.2f, 10.0f);
    c.revMix = std::clamp(c.revMix, 0.0f, 1.0f);
    target = c;
    if (!paramsSnapped) {
        mixL = c.mixL;
        mixR = c.mixR;
        feedback = c.feedback;
        timeL = c.timeLms;
        timeR = c.timeRms;
        modRate = c.modRateHz;
        modDepth = c.modDepthMs;
        revDecaySec = c.revDecaySec;
        revMix = c.revMix;
        paramsSnapped = true;
    }
}

void StereoDelayHallEngine::process(float* left, float* right, int numSamples) {
    const float sr = (float) sampleRate;
    const int lineSize = (int) lineL.buf.size();
    const float maxOffset = (float) (lineSize - 4);
    reverb.setDecay((double) target.revDecaySec, sampleRate);

    for (int i = 0; i < numSamples; ++i) {
        mixL += (target.mixL - mixL) * smoothCoef;
        mixR += (target.mixR - mixR) * smoothCoef;
        feedback += (target.feedback - feedback) * smoothCoef;
        timeL += (target.timeLms - timeL) * smoothCoef;
        timeR += (target.timeRms - timeR) * smoothCoef;
        modRate += (target.modRateHz - modRate) * smoothCoef;
        modDepth += (target.modDepthMs - modDepth) * smoothCoef;
        revMix += (target.revMix - revMix) * smoothCoef;

        const float inL = left[i];
        const float inR = right[i];

        const float lfoL = std::sin(2.0f * 3.14159265358979f * lfoPhase);
        const float lfoR = std::sin(2.0f * 3.14159265358979f * (lfoPhase + 0.5f));
        lfoPhase += modRate / sr;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        const float depthSamples = modDepth * sr * 0.001f;
        float offL = timeL * sr * 0.001f + lfoL * depthSamples;
        float offR = timeR * sr * 0.001f + lfoR * depthSamples;
        offL = std::clamp(offL, 4.0f, maxOffset);
        offR = std::clamp(offR, 4.0f, maxOffset);

        const float dL = lineL.readCubic(offL);
        const float dR = lineR.readCubic(offR);

        lineL.write(inL + feedback * dL);
        lineL.advance();
        lineR.write(inR + feedback * dR);
        lineR.advance();

        float rvL = 0.0f, rvR = 0.0f;
        reverb.process(dL, dR, rvL, rvR);

        const float wetL = dL + revMix * rvL;
        const float wetR = dR + revMix * rvR;

        left[i] = (1.0f - mixL) * inL + mixL * wetL;
        right[i] = (1.0f - mixR) * inR + mixR * wetR;
    }
}

void StereoDelayHallEngine::reset() {
    lineL.clear();
    lineR.clear();
    reverb.clear();
    lfoPhase = 0.0f;
    mixL = target.mixL;
    mixR = target.mixR;
    feedback = target.feedback;
    timeL = target.timeLms;
    timeR = target.timeRms;
    modRate = target.modRateHz;
    modDepth = target.modDepthMs;
    revDecaySec = target.revDecaySec;
    revMix = target.revMix;
    paramsSnapped = true;
}

void StereoDelayHallEngine::HallReverb::prepare(double sr) {
    const double scale = sr / 44100.0;
    const int combT[numCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    const int apT[numAllpass] = {556, 441, 341, 225};
    const int spread = (int) std::lround(23.0 * scale);
    for (int i = 0; i < numCombs; ++i) {
        combsL[i].resize(std::max(16, (int) std::lround(combT[i] * scale)));
        combsR[i].resize(std::max(16, (int) std::lround(combT[i] * scale) + spread));
    }
    for (int i = 0; i < numAllpass; ++i) {
        apL[i].resize(std::max(8, (int) std::lround(apT[i] * scale)));
        apR[i].resize(std::max(8, (int) std::lround(apT[i] * scale) + spread));
    }
    clear();
}

void StereoDelayHallEngine::HallReverb::setDecay(double sec, double sr) {
    const double avgLen = 1390.0 * sr / 44100.0;
    double fb = std::exp(-6.9 * avgLen / (sec * sr)); // ~RT60 mapping
    fb = std::min(0.985, std::max(0.50, fb));
    for (int i = 0; i < numCombs; ++i) {
        combsL[i].feedback = (float) fb;
        combsR[i].feedback = (float) fb;
        combsL[i].damp1 = 0.30f;
        combsR[i].damp1 = 0.30f;
        combsL[i].damp2 = 0.70f;
        combsR[i].damp2 = 0.70f;
    }
}

void StereoDelayHallEngine::HallReverb::clear() {
    for (int i = 0; i < numCombs; ++i) {
        combsL[i].clear();
        combsR[i].clear();
    }
    for (int i = 0; i < numAllpass; ++i) {
        apL[i].clear();
        apR[i].clear();
    }
}

} // namespace dsp
