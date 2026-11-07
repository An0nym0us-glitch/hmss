#include "AudioRoutingMatrix.h"

#include <algorithm>

namespace ab {

const Route* AudioRoutingMatrix::find(const std::string& in, const std::string& out) const
{
    for (const auto& r : routes_)
        if (r.inputId == in && r.outputId == out)
            return &r;
    return nullptr;
}

bool AudioRoutingMatrix::isRouted(const std::string& in, const std::string& out) const
{
    return find(in, out) != nullptr;
}

bool AudioRoutingMatrix::setRoute(const std::string& in, const std::string& out, bool enabled)
{
    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [&](const Route& r) { return r.inputId == in && r.outputId == out; });
    if (enabled) {
        if (it != routes_.end())
            return false;
        routes_.push_back(Route{in, out, 1.0f});
        return true;
    }
    if (it == routes_.end())
        return false;
    routes_.erase(it);
    return true;
}

bool AudioRoutingMatrix::setRouteGain(const std::string& in, const std::string& out, float gain)
{
    for (auto& r : routes_)
        if (r.inputId == in && r.outputId == out) {
            r.gain = std::max(0.0f, std::min(gain, 2.0f));
            return true;
        }
    return false;
}

void AudioRoutingMatrix::removeDevice(const std::string& id)
{
    routes_.erase(std::remove_if(routes_.begin(), routes_.end(),
                                 [&](const Route& r) { return r.inputId == id || r.outputId == id; }),
                  routes_.end());
}

std::vector<Route> AudioRoutingMatrix::routesFrom(const std::string& in) const
{
    std::vector<Route> v;
    for (const auto& r : routes_)
        if (r.inputId == in)
            v.push_back(r);
    return v;
}

std::vector<Route> AudioRoutingMatrix::routesTo(const std::string& out) const
{
    std::vector<Route> v;
    for (const auto& r : routes_)
        if (r.outputId == out)
            v.push_back(r);
    return v;
}

AudioRoutingMatrix::FeedbackRisk AudioRoutingMatrix::feedbackRisk(const DeviceInfo& in, const DeviceInfo& out)
{
    if (sameHardwareContainer(in, out)) {
        // Built-in endpoints of a PC share the "computer" container; headsets share theirs.
        if (in.bus == DeviceBus::Internal && out.bus == DeviceBus::Internal)
            return FeedbackRisk::SameComputer;
        return FeedbackRisk::SameDevice;
    }
    // Without container information, built-in microphone + built-in speakers of the
    // same machine is still the most likely feedback setup.
    if (in.containerId.empty() && out.containerId.empty() && in.bus == DeviceBus::Internal &&
        out.bus == DeviceBus::Internal && out.formFactor == FormFactor::Speakers)
        return FeedbackRisk::SameComputer;
    return FeedbackRisk::None;
}

} // namespace ab
