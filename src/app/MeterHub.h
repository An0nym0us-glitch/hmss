// Reads every real-time meter of the AudioGraph exactly once per UI frame (meters are
// read-and-reset, so several widgets must not read them independently) and keeps the
// values for all pages to display.
#pragma once

#include "core/AudioGraph.h"

struct MeterFrame {
    float inPeak[ab::kMaxInputs] = {};
    bool inClip[ab::kMaxInputs] = {};
    float outPeak[ab::kMaxOutputs] = {};
    bool outClip[ab::kMaxOutputs] = {};
    bool outFeedback[ab::kMaxOutputs] = {};
    float outLatencyMs[ab::kMaxOutputs] = {};
    float outDriftPpm[ab::kMaxOutputs] = {};
    float outLimiterDb[ab::kMaxOutputs] = {};
    unsigned long long outUnderruns[ab::kMaxOutputs] = {};
    float routePeak[ab::kMaxInputs][ab::kMaxOutputs] = {};
    float masterPeak = 0.0f;
};

class MeterHub {
public:
    explicit MeterHub(const ab::AudioGraph& g) : graph_(const_cast<ab::AudioGraph&>(g)) {}

    void poll()
    {
        MeterFrame& f = frame_;
        f.masterPeak = 0.0f;
        for (int i = 0; i < ab::kMaxInputs; ++i) {
            f.inPeak[i] = graph_.inputs[i].meter.takePeak();
            f.inClip[i] = graph_.inputs[i].meter.takeClip();
        }
        for (int o = 0; o < ab::kMaxOutputs; ++o) {
            ab::OutputSlot& s = graph_.outputs[o];
            f.outPeak[o] = s.meter.takePeak();
            f.outClip[o] = s.meter.takeClip();
            f.outFeedback[o] = s.feedbackTripped.load(std::memory_order_relaxed);
            f.outLatencyMs[o] = s.routeLatencyMs.load(std::memory_order_relaxed);
            f.outDriftPpm[o] = s.driftPpm.load(std::memory_order_relaxed);
            f.outLimiterDb[o] = s.limiterReductionDb.load(std::memory_order_relaxed);
            f.outUnderruns[o] = s.underruns.load(std::memory_order_relaxed);
            if (s.active.load(std::memory_order_relaxed))
                f.masterPeak = std::max(f.masterPeak, f.outPeak[o]);
        }
        for (int i = 0; i < ab::kMaxInputs; ++i)
            for (int o = 0; o < ab::kMaxOutputs; ++o)
                f.routePeak[i][o] = graph_.routes[i][o].enabled.load(std::memory_order_relaxed)
                                        ? graph_.routes[i][o].meter.takePeak()
                                        : 0.0f;
    }

    const MeterFrame& frame() const { return frame_; }

private:
    ab::AudioGraph& graph_;
    MeterFrame frame_;
};
