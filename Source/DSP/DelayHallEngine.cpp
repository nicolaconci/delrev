#include "DelayHallEngine.h"

#include <algorithm>

namespace dsp {

static float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

// ratio has discrete steps 1.5, 2, 2.5, 3, 3.5, 4 (x the channel's main delay time)
static float snapRatio(float r) {
    r = std::clamp(r, 1.5f, 4.0f);
    return std::round(r * 2.0f) * 0.5f;
}

void StereoDelayHallEngine::prepare(double sr, int /*maxBlockSize*/) {
    sampleRate = sr;
    smoothCoef = 1.0f - std::exp(-1.0f / (float) (sr * 0.005)); // ~5 ms smoothing
    const int maxSamples = (int) std::ceil(maxTimeMs * 0.001 * sr)
                         + (int) std::ceil(maxModDepthMs * 0.001 * sr) + 8;
    lineL.resize(maxSamples);
    lineR.resize(maxSamples);
    const int pLineMax = (int) std::ceil(maxTimeMs * 4.0f * 0.001 * sr) + 64; // ratio up to 4:1
    chL.prepare(sr, pLineMax);
    chR.prepare(sr, pLineMax);
    pitchRev.prepare(sr);
    pOffL = pOffR = std::clamp(375.0f * (float) sr * 0.001f * 2.0f, 4.0f, (float) (pLineMax - 4)); // default time x default ratio
    inLpL = inLpR = 0.0f;
    inPrevL = inPrevR = 0.0f;
    fastEnvL = fastEnvR = 0.0f;
    refEnvL = refEnvR = 0.0f;
    rampL = rampR = 1.0f;
    swellTargetL = swellTargetR = 1.0f;
    onsetTimerL = onsetTimerR = 1 << 30;
    reverb.prepare(sr);
    EngineParams def;
    target = def;
    targetPreGain = dbToLinear(def.preGainDb);
    targetMaster = dbToLinear(def.masterDb);
    targetWetGain = dbToLinear(def.wetGainDb);
    preGain = targetPreGain;
    master = targetMaster;
    wetGain = targetWetGain;
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
    revWidth = def.revWidth;
    grit = def.grit;
    modVol = def.modVol;
    pitchL = def.pitchL;
    pitchR = def.pitchR;
    ratioL = def.ratioL;
    ratioR = def.ratioR;
    pitchMixL = def.pitchMixL;
    pitchMixR = def.pitchMixR;
    gateL = gateR = 0.0f;
    gritInL = gritOutL = gritInR = gritOutR = 0.0f;
    chL.clear();
    chR.clear();
    pOffL = std::clamp(target.timeLms * (float) sampleRate * 0.001f * snapRatio(target.ratioL), 4.0f, (float) (chL.line.buf.size() - 4));
    pOffR = std::clamp(target.timeRms * (float) sampleRate * 0.001f * snapRatio(target.ratioR), 4.0f, (float) (chR.line.buf.size() - 4));
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
    c.revWidth = std::clamp(c.revWidth, 0.0f, 2.0f);
    c.wetGainDb = std::clamp(c.wetGainDb, -12.0f, 12.0f);
    c.grit = std::clamp(c.grit, 0.0f, 1.0f);
    c.modVol = std::clamp(c.modVol, 0.0f, 1.0f);
    c.pitchL = std::clamp(c.pitchL, -12.0f, 12.0f);
    c.pitchR = std::clamp(c.pitchR, -12.0f, 12.0f);
    c.ratioL = std::clamp(c.ratioL, 1.5f, 4.0f);
    c.ratioR = std::clamp(c.ratioR, 1.5f, 4.0f);
    c.pitchMixL = std::clamp(c.pitchMixL, 0.0f, 1.0f);
    c.pitchMixR = std::clamp(c.pitchMixR, 0.0f, 1.0f);
    target = c;
    targetPreGain = dbToLinear(c.preGainDb);
    targetMaster = dbToLinear(c.masterDb);
    targetWetGain = dbToLinear(c.wetGainDb);
    if (!paramsSnapped) {
        preGain = targetPreGain;
        master = targetMaster;
        wetGain = targetWetGain;
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
        revWidth = c.revWidth;
        grit = c.grit;
        modVol = c.modVol;
        pitchL = c.pitchL;
        pitchR = c.pitchR;
        ratioL = c.ratioL;
        ratioR = c.ratioR;
        pitchMixL = c.pitchMixL;
        pitchMixR = c.pitchMixR;
        paramsSnapped = true;
    }
}

void StereoDelayHallEngine::process(float* left, float* right, int numSamples) {
    const float sr = (float) sampleRate;
    const int lineSize = (int) lineL.buf.size();
    const float maxOffset = (float) (lineSize - 4);
    reverb.setDecay((double) target.revDecaySec, sampleRate);
    pitchRev.setDecay((double) target.revDecaySec, sampleRate);

    for (int i = 0; i < numSamples; ++i) {
        preGain += (targetPreGain - preGain) * smoothCoef;
        master += (targetMaster - master) * smoothCoef;
        wetGain += (targetWetGain - wetGain) * smoothCoef;
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
        revWidth += (target.revWidth - revWidth) * smoothCoef;
        grit += (target.grit - grit) * smoothCoef;
        modVol += (target.modVol - modVol) * smoothCoef;
        pitchL += (target.pitchL - pitchL) * smoothCoef;
        pitchR += (target.pitchR - pitchR) * smoothCoef;
        ratioL += (target.ratioL - ratioL) * smoothCoef;
        ratioR += (target.ratioR - ratioR) * smoothCoef;
        pitchMixL += (target.pitchMixL - pitchMixL) * smoothCoef;
        pitchMixR += (target.pitchMixR - pitchMixR) * smoothCoef;

        const float inL = left[i] * preGain;
        const float inR = right[i] * preGain;

        const float lfoL = std::sin(2.0f * 3.14159265358979f * lfoPhase);
        const float lfoR = std::sin(2.0f * 3.14159265358979f * (lfoPhase + 0.5f));
        lfoPhase += modRate / sr;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        const float depthSamples = modDepth * sr * 0.001f * modVol;
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
        // fast + 15 ms-reference envelopes (used by swell onset and pitch gate)
        {
            const float relF = 1.0f - std::exp(-1.0f / (0.001f * sr));
            const float tauR = 1.0f - std::exp(-1.0f / (0.015f * sr));
            const float aL = std::fabs(inL), aR = std::fabs(inR);
            if (aL > fastEnvL) fastEnvL = aL; else fastEnvL += (aL - fastEnvL) * relF;
            if (aL > refEnvL) refEnvL += (aL - refEnvL) * tauR; else refEnvL += (aL - refEnvL) * tauR;
            if (aR > fastEnvR) fastEnvR = aR; else fastEnvR += (aR - fastEnvR) * relF;
            if (aR > refEnvR) refEnvR += (aR - refEnvR) * tauR; else refEnvR += (aR - refEnvR) * tauR;
        }
        float gL = 1.0f, gR = 1.0f;
        const int S = (int) std::lround(swell * sr * 0.001f);
        if (S > 0) {
            const int refractory = (int) (0.060f * sr); // 60 ms between onsets
            auto rampUp = [&](float& fast, float& ref, float& ramp, float& tgt, int& sinceOnset) {
                ++sinceOnset;
                // onset: fast envelope jumps well above its own recent reference
                // (2.5x) - level-independent and works even in continuous playing;
                // tiny absolute floor against silence; refractory elapsed
                if (fast > 4.0f * ref && fast > 0.005f && sinceOnset > refractory && tgt > 0.0f) {
                    tgt = 0.0f;
                    sinceOnset = 0;
                }
                if (tgt == 0.0f) {
                    ramp -= 1.0f / (0.020f * sr); // smooth reset: 20 ms fade-out down to 0.3 (no hard cut, no click)
                    if (ramp <= 0.3f) { ramp = 0.3f; tgt = 1.0f; } // partial dip (0.3), not a hard cut
                } else if (ramp < 1.0f) {
                    ramp += 1.0f / (float) S;     // linear swell-in over S
                }
                return ramp;
            };
            gL = rampUp(fastEnvL, refEnvL, rampL, swellTargetL, onsetTimerL);
            gR = rampUp(fastEnvR, refEnvR, rampR, swellTargetR, onsetTimerR);
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

        // extra pitch lines: at each transient, capture the recent input (ring buffer),
        // transpose it (read rate 2^(st/12)) and hold it (loop); the held snippet is
        // echoed at ratio x main time (ratio snapped to powers of 2). pitch 0 st = bypass.
        const float rL = snapRatio(ratioL);
        const float rR = snapRatio(ratioR);
        const float readRateL = std::pow(2.0f, pitchL / 12.0f);
        const float readRateR = std::pow(2.0f, pitchR / 12.0f);
        const float bypassL = (std::fabs(pitchL) < 0.01f) ? 0.0f : 1.0f;
        const float bypassR = (std::fabs(pitchR) < 0.01f) ? 0.0f : 1.0f;
        // gate + capture trigger share the onset detector: on at the transient, held while playing
        {
            const float relG = 1.0f - std::exp(-1.0f / (0.300f * sr));
            // long swell-like gate attack, slower at high pitch (readRate up to 2 at +12 st):
            // attack time = 20 ms x readRate, clamped to 10..80 ms (~40 ms at +12 st)
            const float atkL = 1.0f - std::exp(-1.0f / (std::clamp(20.0f * readRateL, 10.0f, 80.0f) * 0.001f * sr));
            const float atkR = 1.0f - std::exp(-1.0f / (std::clamp(20.0f * readRateR, 10.0f, 80.0f) * 0.001f * sr));
            const bool onsetL = fastEnvL > 2.5f * refEnvL && fastEnvL > 0.005f;
            const bool onsetR = fastEnvR > 2.5f * refEnvR && fastEnvR > 0.005f;
            if (onsetL || fastEnvL > 0.01f) gateL += (1.0f - gateL) * atkL; else gateL *= (1.0f - relG);
            if (onsetR || fastEnvR > 0.01f) gateR += (1.0f - gateR) * atkR; else gateR *= (1.0f - relG);
            chL.push(wInL * gL);
            chR.push(wInR * gR);
        }
        // pitch-line delay offsets are smoothed: changing Time or Ratio glides instead of jumping
        const float maxPOff = (float) (chL.line.buf.size() - 4);
        const float tgtOffL = std::clamp(timeL * sr * 0.001f * rL, 4.0f, maxPOff);
        const float tgtOffR = std::clamp(timeR * sr * 0.001f * rR, 4.0f, maxPOff);
        pOffL += (tgtOffL - pOffL) * smoothCoef;
        pOffR += (tgtOffR - pOffR) * smoothCoef;
        float extraL = 0.0f, extraR = 0.0f;
        if (bypassL > 0.0f) {
            chL.line.write(chL.play(readRateL));
            chL.line.advance();
            extraL = chL.line.readCubic(pOffL);
        }
        if (bypassR > 0.0f) {
            chR.line.write(chR.play(readRateR));
            chR.line.advance();
            extraR = chR.line.readCubic(pOffR);
        }
        extraL *= gateL;
        extraR *= gateR;

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

        // grit: soft-clip (tanh) distortion on the repeats with slow RMS make-up gain,
        // so the perceived volume stays constant and only saturation is added
        if (grit > 0.001f) {
            const float drive = 1.0f + grit * 6.0f;
            const float oL = std::tanh(wL * drive);
            const float oR = std::tanh(wR * drive);
            const float k = 1.0f - std::exp(-1.0f / (0.150f * sr)); // 150 ms loudness follower
            gritInL += (wL * wL - gritInL) * k;
            gritOutL += (oL * oL - gritOutL) * k;
            gritInR += (wR * wR - gritInR) * k;
            gritOutR += (oR * oR - gritOutR) * k;
            const float mkL = std::clamp(gritOutL > 1e-9f ? std::sqrt(gritInL / gritOutL) : 1.0f, 0.25f, 2.0f);
            const float mkR = std::clamp(gritOutR > 1e-9f ? std::sqrt(gritInR / gritOutR) : 1.0f, 0.25f, 2.0f);
            wL = oL * mkL;
            wR = oR * mkR;
        }

        float rvL = 0.0f, rvR = 0.0f;
        // main hall: fed by the main repeats only
        reverb.process(wL, wR, rvL, rvR);
        // pitch chain: original signal -> capture/transpose/hold -> delay at ratio x Time
        // -> dedicated hall -> Pitch Mix (the gate is AFTER the reverb, so Pitch Mix 0
        // silences the pitch completely, tail included)
        float prL = 0.0f, prR = 0.0f;
        pitchRev.process(extraL, extraR, prL, prR);
        {
            const float mid = 0.5f * (prL + prR);
            const float side = 0.5f * (prL - prR) * revWidth;
            prL = mid + side;
            prR = mid - side;
        }
        {
            // reverb width in mid/side: 0 = mono tail, 1 = full stereo, 2 = extra wide
            const float mid = 0.5f * (rvL + rvR);
            const float side = 0.5f * (rvL - rvR) * revWidth;
            rvL = mid + side;
            rvR = mid - side;
        }

        const float wetL = (wL + revMix * rvL) * wetGain;
        const float wetR = (wR + revMix * rvR) * wetGain;

        const float dryL = target.swellOnInput ? inL * gL : inL;
        const float dryR = target.swellOnInput ? inR * gR : inR;
        // Mix L/R is the master of the whole channel branch, pitch line included:
        // pitch level = Mix x Pitch Mix (at Mix 0 the pitch is silent too)
        left[i] = (((1.0f - mixL) * dryL + mixL * wetL) + mixL * pitchMixL * (extraL + revMix * prL) * wetGain) * master;
        right[i] = (((1.0f - mixR) * dryR + mixR * wetR) + mixR * pitchMixR * (extraR + revMix * prR) * wetGain) * master;
    }
}

void StereoDelayHallEngine::reset() {
    lineL.clear();
    lineR.clear();
    reverb.clear();
    pitchRev.clear();
    lfoPhase = 0.0f;
    preGain = targetPreGain;
    master = targetMaster;
    wetGain = targetWetGain;
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
    refEnvL = refEnvR = 0.0f;
    rampL = rampR = 1.0f;
    swellTargetL = swellTargetR = 1.0f;
    onsetTimerL = onsetTimerR = 1 << 30;
    timeL = target.timeLms;
    timeR = target.timeRms;
    modRate = target.modRateHz;
    modDepth = target.modDepthMs;
    revDecaySec = target.revDecaySec;
    revMix = target.revMix;
    revWidth = target.revWidth;
    grit = target.grit;
    modVol = target.modVol;
    pitchL = target.pitchL;
    pitchR = target.pitchR;
    ratioL = target.ratioL;
    ratioR = target.ratioR;
    pitchMixL = target.pitchMixL;
    pitchMixR = target.pitchMixR;
    gateL = gateR = 0.0f;
    gritInL = gritOutL = gritInR = gritOutR = 0.0f;
    chL.clear();
    chR.clear();
    pOffL = std::clamp(target.timeLms * (float) sampleRate * 0.001f * snapRatio(target.ratioL), 4.0f, (float) (chL.line.buf.size() - 4));
    pOffR = std::clamp(target.timeRms * (float) sampleRate * 0.001f * snapRatio(target.ratioR), 4.0f, (float) (chR.line.buf.size() - 4));
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
