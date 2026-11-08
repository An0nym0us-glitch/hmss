// Clock-drift compensation for one route (input device clock -> output device clock).
//
// Every audio device runs on its own crystal. A microphone nominally at 48 kHz may
// really deliver 48 003 Hz while the speaker consumes 47 998 Hz; without correction
// the route buffer slowly fills up (latency grows) or drains (dropouts every few
// minutes). The controller observes the route buffer fill level and returns a small
// ratio correction (typically < 0.05 %) that is fed to the route's resampler, keeping
// the buffer at its latency target indefinitely. This is a PI controller acting on a
// low-pass filtered fill error.
#pragma once

#include <cmath>

namespace ab {

class DriftController {
public:
    // kp [1/s], ki [1/s^2] act on the fill error expressed in seconds.
    void configure(double kp = 0.06, double ki = 0.002, double smoothingSeconds = 0.5, double maxCorrection = 0.01)
    {
        kp_ = kp;
        ki_ = ki;
        tau_ = smoothingSeconds;
        maxCorr_ = maxCorrection;
    }

    void reset()
    {
        avg_ = 0.0;
        integ_ = 0.0;
        primed_ = false;
    }

    // Keeps the learned clock offset (integrator) but forgets the error history.
    // Used after a resynchronisation so the route does not need to re-learn the drift.
    void softReset()
    {
        avg_ = 0.0;
        primed_ = false;
    }

    // errorSeconds: (fill - target) / engineRate. dtSeconds: wall time represented by
    // this output callback. Returns the factor to multiply the nominal ratio with.
    double update(double errorSeconds, double dtSeconds)
    {
        if (!primed_) {
            avg_ = errorSeconds;
            primed_ = true;
        } else {
            const double a = 1.0 - std::exp(-dtSeconds / tau_);
            avg_ += (errorSeconds - avg_) * a;
        }
        integ_ += avg_ * dtSeconds;
        const double integLimit = maxCorr_ / (ki_ > 0 ? ki_ : 1.0);
        if (integ_ > integLimit) integ_ = integLimit;
        if (integ_ < -integLimit) integ_ = -integLimit;
        double corr = kp_ * avg_ + ki_ * integ_;
        if (corr > maxCorr_) corr = maxCorr_;
        if (corr < -maxCorr_) corr = -maxCorr_;
        lastCorrection_ = corr;
        return 1.0 + corr;
    }

    double lastCorrectionPpm() const { return lastCorrection_ * 1e6; }
    double smoothedErrorSeconds() const { return avg_; }

private:
    double kp_ = 0.06, ki_ = 0.002, tau_ = 0.5, maxCorr_ = 0.01;
    double avg_ = 0.0, integ_ = 0.0, lastCorrection_ = 0.0;
    bool primed_ = false;
};

} // namespace ab
