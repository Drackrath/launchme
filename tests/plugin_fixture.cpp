#include "../sdk/launchme_plugin.h"
#include "../src/plugins/json.hpp"
#include "../src/platform/text_encoding.hpp"
#include <filesystem>
#include <fstream>

namespace {
std::filesystem::path folder;
void log(const char* event) { if (!folder.empty()) std::ofstream(folder / L"lifecycle.txt", std::ios::app | std::ios::binary) << event << '\n'; }
int32_t __cdecl invoke(const char* bytes, uint32_t length, LaunchmeEmitV1 emit, void* context) {
    using namespace plugins::wire;
    try {
        const auto input = json::parse(std::string_view(bytes, length)); if (!input) return 1;
        const auto method = text(*input, "method");
        std::string response = "{}";
        if (method == "activate") { folder = text_encoding::wide(text(field(*input, "params"), "dataDirectory")); log("activate"); }
        else if (method == "deactivate") log("deactivate");
        else if (text(field(*input, "params"), "command") == "broken") response = "[not-json";
        else response = "{\"settings\":" + encode(field(*input, "settings")) + '}';
        emit(context, response.data(), static_cast<uint32_t>(response.size())); return 0;
    } catch (...) { return 1; }
}
#ifdef BAD_PLUGIN_ABI
const LaunchmePluginV1 api{sizeof(LaunchmePluginV1), 99, invoke};
#else
const LaunchmePluginV1 api{sizeof(LaunchmePluginV1), LAUNCHME_PLUGIN_ABI, invoke};
#endif
}
extern "C" __declspec(dllexport) const LaunchmePluginV1* __cdecl launchme_plugin_v1(uint32_t) { return &api; }
