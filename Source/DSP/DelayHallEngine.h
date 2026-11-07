#pragma once

#include <algorithm>
#include <vector>
#include <cmath>

namespace dsp {

struct EngineParams {
    float preGainDb = 0.0f;   // input gain (dB), applied before everything
    float masterDb = 0.0f;    // output/master volume (dB)
    float mixL = 0.30f;       // 0..1 dry/wet, left channel
    float mixR = 0.30f;       // 0..1 dry/wet, right channel
    float toneL = 1.0f;       // 0..1 lowpass on the left wet (1 = bright ~18 kHz, 0 = dark ~300 Hz)
    float toneR = 1.0f;       // 0..1 lowpass on the right wet
    float feedbackL = 0.35f;  // 0..0.95 recirculation gain, left channel
    float feedbackR = 0.35f;  // 0..0.95 recirculation gain, right channel
    float smear = 0.0f;       // bipolar: <0 = smear lowpass on the line input, >0 = crystal highpass; 0 = off
    float swellMs = 0.0f;     // 0..maxSwellMs: volume swell-in (time ramp) per repeat, baked into the line input
    bool swellOnInput = false; // true: the same swell is also applied to the dry input signal
    float timeLms = 375.0f;   // delay time, left channel (ms)
    float timeRms = 500.0f;   // delay time, right channel (ms)
    float modRateHz = 0.8f;   // modulation rate (Hz)
    float modDepthMs = 4.0f;  // modulation depth, as delay-time deviation (ms)
    float revDecaySec = 3.0f; // hall reverb decay, RT60-like (s)
    float revMix = 0.35f;     // reverb amount on the repeats only (0..1)
    float revWidth = 1.0f;    // reverb stereo width: 0 = mono tail, 1 = full, 2 = extra wide
    float wetGainDb = 0.0f;   // gain on the wet path only (useful on aux buses at 100% wet)
    float grit = 0.0f;        // 0..1 soft-clip distortion on the repeats
    float modVol = 1.0f;      // 0..1 modulator (LFO) volume
    float pitchL = 0.0f;      // extra line L pitch shift (semitones, -12..+12, 0 = bypass)
    float pitchR = 0.0f;      // extra line R pitch shift (semitones)
    float ratioL = 1.0f;      // extra line L time = ratio x main time (0.25..4, snapped to powers of 2)
    float ratioR = 1.0f;      // extra line R time ratio
    float pitchMixL = 0.5f;   // extra line L level (0..1), applied post-mix (independent of Mix L)
    float pitchMixR = 0.5f;   // extra line R level
};

// Stereo delay with independent L/R times, time modulation, feedback, and a
// hall reverb fed exclusively by the delayed signal (each repeat gets its own
// reverb tail; the dry signal is never sent to the reverb).
class StereoDelayHallEngine {
public:
    void prepare(double sampleRate, int maxBlockSize);
    void setParams(const EngineParams& p); // call once per block on the audio thread
    void process(float* left, float* right, int numSamples);
    void reset();

    static constexpr float maxTimeMs = 2000.0f;
    static constexpr float maxModDepthMs = 30.0f;
    static constexpr float minTimeMs = 5.0f;
    static constexpr float maxSwellMs = 500.0f;

private:
    struct DelayLine {
        std::vector<float> buf;
        int writePos = 0;
        void resize(int n) { buf.assign((size_t) n, 0.0f); writePos = 0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); writePos = 0; }
        inline void write(float x) { buf[(size_t) writePos] = x; }
        inline void advance() { if (++writePos >= (int) buf.size()) writePos = 0; }
        inline float readCubic(float offset) const;
    };

    // extra delay line with pitch shift (dual-tap crossfade, DAFX-style)
    struct PitchLine {
        std::vector<float> buf;
        int writePos = 0;
        float rp = 0.0f;      // read position (absolute, in buffer coords)
        float window = 0.0f;  // crossfade window W
        void prepare(double sr, int maxSamples) {
            buf.assign((size_t) maxSamples + 8192, 0.0f);
            window = std::min((float) maxSamples * 0.5f, 0.05f * (float) sr);
            rp = -window;
        }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); writePos = 0; rp = -window; }
        inline float readAt(float pos) const {
            const int size = (int) buf.size();
            double p = std::fmod((double) pos, (double) size);
            if (p < 0.0) p += (double) size;
            const double fl = std::floor(p);
            const int i0 = (int) fl;
            const float frac = (float) (p - fl);
            const int im1 = ((i0 - 1) % size + size) % size;
            const int i0w = ((i0) % size + size) % size;
            const int i1 = ((i0 + 1) % size + size) % size;
            const int i2 = ((i0 + 2) % size + size) % size;
            const float ym1 = buf[(size_t) im1], y0 = buf[(size_t) i0w];
            const float y1 = buf[(size_t) i1], y2 = buf[(size_t) i2];
            const float c0 = y0;
            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        }
        inline float process(float in, float ratio, float baseOffset) {
            const int size = (int) buf.size();
            rp += ratio;
            const float minD = std::max(4.0f, baseOffset);
            const float maxD = minD + window;
            const float wp = (float) writePos;
            while (wp - rp > maxD) rp += window;
            while (wp - rp < minD) rp -= window;
            float w = (wp - rp - minD) / window;
            w = std::clamp(w, 0.0f, 1.0f);
            const float out = w * readAt(rp) + (1.0f - w) * readAt(rp - window);
            buf[(size_t) writePos] = in;
            writePos = (writePos + 1) % size;
            return out;
        }
    };

    struct Comb {
        std::vector<float> buf;
        int idx = 0;
        float filterstore = 0.0f;
        float feedback = 0.0f;
        float damp1 = 0.0f;
        float damp2 = 0.0f;
        void resize(int n) { buf.assign((size_t) n, 0.0f); idx = 0; filterstore = 0.0f; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; filterstore = 0.0f; }
        inline float process(float in) {
            float out = buf[(size_t) idx];
            filterstore = out * damp2 + filterstore * damp1;
            buf[(size_t) idx] = in + filterstore * feedback + 1e-20f;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };

    struct Allpass {
        std::vector<float> buf;
        int idx = 0;
        void resize(int n) { buf.assign((size_t) n, 0.0f); idx = 0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; }
        inline float process(float in) {
            float b = buf[(size_t) idx];
            float y = b - 0.5f * in;
            buf[(size_t) idx] = in + 0.5f * y;
            if (++idx >= (int) buf.size()) idx = 0;
            return y;
        }
    };

    struct HallReverb {
        static constexpr int numCombs = 8;
        static constexpr int numAllpass = 4;
        Comb combsL[numCombs], combsR[numCombs];
        Allpass apL[numAllpass], apR[numAllpass];
        void prepare(double sr);
        void setDecay(double sec, double sr);
        void clear();
        inline void process(float inL, float inR, float& outL, float& outR) {
            constexpr float kIn = 0.25f;
            constexpr float kOut = 0.4f;
            float l = inL * kIn;
            float r = inR * kIn;
            float sl = 0.0f, sr = 0.0f;
            for (int i = 0; i < numCombs; ++i) {
                sl += combsL[i].process(l);
                sr += combsR[i].process(r);
            }
            sl *= kOut;
            sr *= kOut;
            for (int i = 0; i < numAllpass; ++i) {
                sl = apL[i].process(sl);
                sr = apR[i].process(sr);
            }
            outL = sl;
            outR = sr;
        }
    };

    double sampleRate = 44100.0;
    EngineParams target {};
    float preGain = 1.0f, master = 1.0f, wetGain = 1.0f; // smoothed linear gains
    float targetPreGain = 1.0f, targetMaster = 1.0f, targetWetGain = 1.0f;
    float mixL = 0.0f, mixR = 0.0f, toneL = 1.0f, toneR = 1.0f;
    float toneLpL = 0.0f, toneLpR = 0.0f; // tone lowpass state
    float feedbackL = 0.0f, feedbackR = 0.0f;
    // smear state: one-pole lowpass on the delay-line input (per channel)
    float smear = 0.0f;
    float inLpL = 0.0f, inLpR = 0.0f;
    float inPrevL = 0.0f, inPrevR = 0.0f; // for the crystal "glass break" transient (first difference)
    // swell state: onset detector + time ramp on the line input (per channel)
    float swell = 0.0f;
    float fastEnvL = 0.0f, fastEnvR = 0.0f;
    float refEnvL = 0.0f, refEnvR = 0.0f;
    float rampL = 1.0f, rampR = 1.0f;
    float swellTargetL = 1.0f, swellTargetR = 1.0f;
    int onsetTimerL = 1 << 30, onsetTimerR = 1 << 30;
    float grit = 0.0f, modVol = 1.0f, pitchL = 0.0f, pitchR = 0.0f, ratioL = 1.0f, ratioR = 1.0f, pitchMixL = 0.5f, pitchMixR = 0.5f;
    float gateL = 0.0f, gateR = 0.0f; // pitch-line gate: opens at the transient, held while playing
    float gritInL = 0.0f, gritOutL = 0.0f, gritInR = 0.0f, gritOutR = 0.0f; // slow RMS follower: constant-volume grit
    PitchLine plL, plR;
    float timeL = 0.0f, timeR = 0.0f;
    float modRate = 0.0f, modDepth = 0.0f, revDecaySec = 0.0f, revMix = 0.0f, revWidth = 1.0f;
    float smoothCoef = 0.0f;
    bool paramsSnapped = false;
    DelayLine lineL, lineR;
    HallReverb reverb;
    float lfoPhase = 0.0f;
};

inline float StereoDelayHallEngine::DelayLine::readCubic(float offset) const {
    const int size = (int) buf.size();
    const double rp = (double) writePos - (double) offset;
    const double fl = std::floor(rp);
    const int i0 = (int) fl;
    const float frac = (float) (rp - fl);
    const int i0w = ((i0 % size) + size) % size;
    const int im1 = ((i0 - 1) % size + size) % size;
    const int i1 = ((i0 + 1) % size + size) % size;
    const int i2 = ((i0 + 2) % size + size) % size;
    const float ym1 = buf[(size_t) im1];
    const float y0 = buf[(size_t) i0w];
    const float y1 = buf[(size_t) i1];
    const float y2 = buf[(size_t) i2];
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

} // namespace dsp
