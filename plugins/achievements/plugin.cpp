#include "../../sdk/launchme_plugin.h"
#include "achievements.hpp"

namespace {
achievements::Service service;
int32_t LAUNCHME_CALL invoke(const char* bytes, uint32_t length, LaunchmeEmitV1 emit, void* context) {
    if (!bytes || !emit || length > LAUNCHME_PLUGIN_MAX_JSON) return 1;
    try {
        const auto request = json::parse(std::string_view(bytes, length)); if (!request) return 1;
        const auto response = plugins::wire::encode(service.dispatch(*request));
        emit(context, response.data(), static_cast<uint32_t>(response.size())); return 0;
    } catch (...) {
        const std::string response = R"({"message":"Achievements: could not load data."})";
        emit(context, response.data(), static_cast<uint32_t>(response.size())); return 0;
    }
}
const LaunchmePluginV1 api{sizeof(LaunchmePluginV1), LAUNCHME_PLUGIN_ABI, invoke};
}
extern "C" LAUNCHME_PLUGIN_EXPORT const LaunchmePluginV1* LAUNCHME_CALL launchme_plugin_v1(uint32_t abi) { return abi == LAUNCHME_PLUGIN_ABI ? &api : nullptr; }
