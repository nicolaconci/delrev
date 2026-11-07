#include "DelayHallEngine.h"

#include <algorithm>

namespace dsp {

static float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

void StereoDelayHallEngine::prepare(double sr, int /*maxBlockSize*/) {
    sampleRate = sr;
    smoothCoef = 1.0f - std::exp(-1.0f / (float) (sr * 0.005)); // ~5 ms smoothing
    const int maxSamples = (int) std::ceil(maxTimeMs * 0.001 * sr)
                         + (int) std::ceil(maxModDepthMs * 0.001 * sr) + 8;
    lineL.resize(maxSamples);
    lineR.resize(maxSamples);
    peakEnvL = peakEnvR = 0.0f;
    atkEnvL = atkEnvR = 0.0f;
    reverb.prepare(sr);
    EngineParams def;
    target = def;
    targetPreGain = dbToLinear(def.preGainDb);
    targetMaster = dbToLinear(def.masterDb);
    preGain = targetPreGain;
    master = targetMaster;
    mixL = def.mixL;
    mixR = def.mixR;
    toneL = def.toneL;
    toneR = def.toneR;
    toneLpL = toneLpR = 0.0f;
    feedbackL = def.feedbackL;
    feedbackR = def.feedbackR;
    attack = def.attackMs;
    peakEnvL = peakEnvR = 0.0f;
    atkEnvL = atkEnvR = 0.0f;
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
    c.preGainDb = std::clamp(c.preGainDb, -24.0f, 24.0f);
    c.masterDb = std::clamp(c.masterDb, -48.0f, 12.0f);
    c.mixL = std::clamp(c.mixL, 0.0f, 1.0f);
    c.mixR = std::clamp(c.mixR, 0.0f, 1.0f);
    c.toneL = std::clamp(c.toneL, 0.0f, 1.0f);
    c.toneR = std::clamp(c.toneR, 0.0f, 1.0f);
    c.feedbackL = std::clamp(c.feedbackL, 0.0f, 0.95f);
    c.feedbackR = std::clamp(c.feedbackR, 0.0f, 0.95f);
    c.attackMs = std::clamp(c.attackMs, 0.0f, maxAttackMs);
    c.attackCurve = std::clamp(c.attackCurve, 0, 2);
    c.timeLms = std::clamp(c.timeLms, minTimeMs, maxTimeMs);
    c.timeRms = std::clamp(c.timeRms, minTimeMs, maxTimeMs);
    c.modRateHz = std::clamp(c.modRateHz, 0.01f, 20.0f);
    c.modDepthMs = std::clamp(c.modDepthMs, 0.0f, maxModDepthMs);
    c.revDecaySec = std::clamp(c.revDecaySec, 0.2f, 10.0f);
    c.revMix = std::clamp(c.revMix, 0.0f, 1.0f);
    target = c;
    targetPreGain = dbToLinear(c.preGainDb);
    targetMaster = dbToLinear(c.masterDb);
    if (!paramsSnapped) {
        preGain = targetPreGain;
        master = targetMaster;
        mixL = c.mixL;
        mixR = c.mixR;
        toneL = c.toneL;
        toneR = c.toneR;
        feedbackL = c.feedbackL;
        feedbackR = c.feedbackR;
        attack = c.attackMs;
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
        preGain += (targetPreGain - preGain) * smoothCoef;
        master += (targetMaster - master) * smoothCoef;
        mixL += (target.mixL - mixL) * smoothCoef;
        mixR += (target.mixR - mixR) * smoothCoef;
        toneL += (target.toneL - toneL) * smoothCoef;
        toneR += (target.toneR - toneR) * smoothCoef;
        feedbackL += (target.feedbackL - feedbackL) * smoothCoef;
        feedbackR += (target.feedbackR - feedbackR) * smoothCoef;
        attack += (target.attackMs - attack) * smoothCoef;
        timeL += (target.timeLms - timeL) * smoothCoef;
        timeR += (target.timeRms - timeR) * smoothCoef;
        modRate += (target.modRateHz - modRate) * smoothCoef;
        modDepth += (target.modDepthMs - modDepth) * smoothCoef;
        revMix += (target.revMix - revMix) * smoothCoef;

        const float inL = left[i] * preGain;
        const float inR = right[i] * preGain;

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

        // recirculating feedback, raw: analog-style smooth tail
        lineL.write(inL + feedbackL * dL);
        lineL.advance();
        lineR.write(inR + feedbackR * dR);
        lineR.advance();

        // attack: amplitude fade-in per repeat on the wet signal, using TWO
        // smoothed envelopes (peak detector + attack ramp) so the gain never
        // wobbles at the waveform frequency -> no distortion / no high-pass
        // coloration; the body of the repeat is preserved (gain -> 1 after A)
        float wL = dL, wR = dR;
        const int A = (int) std::lround(attack * sr * 0.001f);
        if (A > 0) {
            const float relPeak = 1.0f - std::exp(-1.0f / (0.050f * sr)); // peak env release ~50 ms
            const float relAtk  = 1.0f - std::exp(-1.0f / (0.010f * sr)); // attack env release ~10 ms
            const float pkL = std::fabs(dL);
            const float pkR = std::fabs(dR);
            // peak detector: instant attack, slow release -> tracks the echo envelope
            if (pkL > peakEnvL) peakEnvL = pkL; else peakEnvL += (pkL - peakEnvL) * relPeak;
            if (pkR > peakEnvR) peakEnvR = pkR; else peakEnvR += (pkR - peakEnvR) * relPeak;
            // attack ramp: linear rise over A samples, fast release
            if (peakEnvL > atkEnvL) atkEnvL += peakEnvL / (float) A; // linear rise, reaches peak after A samples
            else                    atkEnvL += (peakEnvL - atkEnvL) * relAtk;
            if (peakEnvR > atkEnvR) atkEnvR += peakEnvR / (float) A;
            else                    atkEnvR += (peakEnvR - atkEnvR) * relAtk;
            float gL = std::clamp(atkEnvL / std::max(peakEnvL, 1e-4f), 0.0f, 1.0f);
            float gR = std::clamp(atkEnvR / std::max(peakEnvR, 1e-4f), 0.0f, 1.0f);
            switch (target.attackCurve) {
                case 1: gL = std::pow(gL, 0.5f); gR = std::pow(gR, 0.5f); break; // log: fast start
                case 2: gL = std::pow(gL, 2.0f); gR = std::pow(gR, 2.0f); break; // exp: slow start
                default: break;                                                 // linear
            }
            wL = dL * gL;
            wR = dR * gR;
        }

        // tone: per-channel one-pole lowpass on the wet (bright vs dark delay);
        // at 100% the filter is bypassed (fully transparent)
        if (toneL < 0.999f) {
            const float fcL = 300.0f * std::pow(60.0f, toneL); // 300 Hz .. 18 kHz
            const float aL = 1.0f - std::exp(-6.2831853f * fcL / sr);
            toneLpL += aL * (wL - toneLpL);
            wL = toneLpL;
        }
        if (toneR < 0.999f) {
            const float fcR = 300.0f * std::pow(60.0f, toneR);
            const float aR = 1.0f - std::exp(-6.2831853f * fcR / sr);
            toneLpR += aR * (wR - toneLpR);
            wR = toneLpR;
        }

        float rvL = 0.0f, rvR = 0.0f;
        reverb.process(wL, wR, rvL, rvR);

        const float wetL = wL + revMix * rvL;
        const float wetR = wR + revMix * rvR;

        left[i] = ((1.0f - mixL) * inL + mixL * wetL) * master;
        right[i] = ((1.0f - mixR) * inR + mixR * wetR) * master;
    }
}

void StereoDelayHallEngine::reset() {
    lineL.clear();
    lineR.clear();
    reverb.clear();
    lfoPhase = 0.0f;
    preGain = targetPreGain;
    master = targetMaster;
    mixL = target.mixL;
    mixR = target.mixR;
    toneL = target.toneL;
    toneR = target.toneR;
    toneLpL = toneLpR = 0.0f;
    feedbackL = target.feedbackL;
    feedbackR = target.feedbackR;
    attack = target.attackMs;
    peakEnvL = peakEnvR = 0.0f;
    atkEnvL = atkEnvR = 0.0f;
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
