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
    inLpL = inLpR = 0.0f;
    inPrevL = inPrevR = 0.0f;
    fastEnvL = fastEnvR = 0.0f;
    slowEnvL = slowEnvR = 0.0f;
    rampL = rampR = 1.0f;
    swellTargetL = swellTargetR = 1.0f;
    onsetTimerL = onsetTimerR = 1 << 30;
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
    smear = def.smear;
    swell = def.swellMs;
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
    c.smear = std::clamp(c.smear, -1.0f, 1.0f);
    c.swellMs = std::clamp(c.swellMs, 0.0f, maxSwellMs);
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
        smear = c.smear;
        swell = c.swellMs;
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
        smear += (target.smear - smear) * smoothCoef;
        swell += (target.swellMs - swell) * smoothCoef;
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

        // swell: volume swell-in per repeat, as a pure time ramp baked into the
        // line input (onset detector resets the ramp; never follows the waveform
        // -> no distortion). Toggle swellOnInput applies the same swell to the
        // dry input signal too.
        float gL = 1.0f, gR = 1.0f;
        const int S = (int) std::lround(swell * sr * 0.001f);
        if (S > 0) {
            const int refractory = std::max(S, (int) (0.200f * sr)); // >= 200 ms between onsets
            auto rampUp = [&](float in, float& fast, float& slow, float& ramp, float& target, int& sinceOnset) {
                const float a = std::fabs(in);
                const float relF = 1.0f - std::exp(-1.0f / (0.001f * sr)); // fast env release ~1 ms
                const float upS  = 1.0f - std::exp(-1.0f / (0.020f * sr)); // slow env attack  ~20 ms
                const float relS = 1.0f - std::exp(-1.0f / (0.400f * sr)); // slow env release ~400 ms
                if (a > fast) fast = a; else fast += (a - fast) * relF;
                if (a > slow) slow += (a - slow) * upS; else slow += (a - slow) * relS;
                ++sinceOnset;
                // marked transient only: fast energy jumps well above slow (3x),
                // absolute level significant, refractory elapsed, previous swell done
                if (fast > 5.0f * slow && slow > 0.15f && sinceOnset > refractory && ramp >= 1.0f && target > 0.0f) {
                    target = 0.0f;
                    sinceOnset = 0;
                }
                if (target == 0.0f) {
                    ramp -= 1.0f / (0.020f * sr); // smooth reset: 20 ms fade-out, floor 0.15 (never a hard cut)
                    if (ramp <= 0.15f) { ramp = 0.15f; target = 1.0f; }
                } else if (ramp < 1.0f) {
                    ramp += 1.0f / (float) S;     // linear swell-in over S
                }
                return ramp;
            };
            gL = rampUp(inL, fastEnvL, slowEnvL, rampL, swellTargetL, onsetTimerL);
            gR = rampUp(inR, fastEnvR, slowEnvR, rampR, swellTargetR, onsetTimerR);
        }

        // smear | crystal: bipolar filter applied ONCE to the input entering the
        // delay line. smear < 0: lowpass (log mapping, acts early) -> the first
        // repeat is born soft and later repeats inherit it via the raw feedback
        // (no compounding). smear > 0: highpass ("crystal"). Linear filters ->
        // no distortion.
        float wInL = inL, wInR = inR;
        if (smear < -0.01f) {
            const float eff = std::pow(-smear, 0.5f);                    // log: acts early
            const float fc = 2000.0f * std::pow(8.0f, 1.0f - eff);       // 2 kHz .. 16 kHz
            const float a = 1.0f - std::exp(-6.2831853f * fc / sr);
            inLpL += a * (inL - inLpL);
            inLpR += a * (inR - inLpR);
            wInL = inLpL;
            wInR = inLpR;
        } else if (smear > 0.01f) {
            // crystal: blend toward highpass (cutoff capped at 2.5 kHz so the
            // repeat keeps its body) PLUS an emphasized fast transient at each
            // onset ("glass break"): the first difference of the input, baked
            // into the line so every repeat starts with the sharp attack
            const float fc = 400.0f * std::pow(6.25f, smear);            // 400 Hz .. 2.5 kHz
            const float a = 1.0f - std::exp(-6.2831853f * fc / sr);
            inLpL += a * (inL - inLpL);
            inLpR += a * (inR - inLpR);
            wInL = inL - smear * inLpL + smear * 1.0f * (inL - inPrevL);
            wInR = inR - smear * inLpR + smear * 1.0f * (inR - inPrevR);
            // gain compensation: the highpass removes energy, so raise the wet
            // level as the crystal amount grows (the repeat must not get lost)
            wInL *= 1.0f + 2.0f * smear;
            wInR *= 1.0f + 2.0f * smear;
        }
        inPrevL = inL;
        inPrevR = inR;

        lineL.write(wInL * gL + feedbackL * dL);
        lineL.advance();
        lineR.write(wInR * gR + feedbackR * dR);
        lineR.advance();

        float wL = dL, wR = dR;

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

        const float dryL = target.swellOnInput ? inL * gL : inL;
        const float dryR = target.swellOnInput ? inR * gR : inR;
        left[i] = ((1.0f - mixL) * dryL + mixL * wetL) * master;
        right[i] = ((1.0f - mixR) * dryR + mixR * wetR) * master;
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
    smear = target.smear;
    inLpL = inLpR = 0.0f;
    inPrevL = inPrevR = 0.0f;
    swell = target.swellMs;
    fastEnvL = fastEnvR = 0.0f;
    slowEnvL = slowEnvR = 0.0f;
    rampL = rampR = 1.0f;
    swellTargetL = swellTargetR = 1.0f;
    onsetTimerL = onsetTimerR = 1 << 30;
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
