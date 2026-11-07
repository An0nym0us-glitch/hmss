// Routing model in terms of persistent endpoint IDs (independent of which devices
// are currently connected, so routes survive disconnects and restarts).
#pragma once

#include "AudioTypes.h"

#include <string>
#include <vector>

namespace ab {

struct Route {
    std::string inputId;
    std::string outputId;
    float gain = 1.0f; // linear
};

class AudioRoutingMatrix {
public:
    bool isRouted(const std::string& in, const std::string& out) const;
    const Route* find(const std::string& in, const std::string& out) const;
    // Returns true if the matrix changed.
    bool setRoute(const std::string& in, const std::string& out, bool enabled);
    bool setRouteGain(const std::string& in, const std::string& out, float gain);
    void removeDevice(const std::string& id);
    void clear() { routes_.clear(); }

    const std::vector<Route>& routes() const { return routes_; }
    void setRoutes(std::vector<Route> r) { routes_ = std::move(r); }
    std::vector<Route> routesFrom(const std::string& in) const;
    std::vector<Route> routesTo(const std::string& out) const;
    std::size_t size() const { return routes_.size(); }

    enum class FeedbackRisk { None, SameDevice, SameComputer };
    // Acoustic feedback risk of routing `in` to `out`. A microphone feeding the speaker
    // of the same physical device (headset, laptop) is the classic howl-around setup.
    static FeedbackRisk feedbackRisk(const DeviceInfo& in, const DeviceInfo& out);

private:
    std::vector<Route> routes_;
};

} // namespace ab
