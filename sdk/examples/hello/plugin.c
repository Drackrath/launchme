#define LAUNCHME_PLUGIN_EXPORTS
#include "launchme_plugin.h"
#include <string.h>

/* A stateless example: lifecycle calls require no work. The host uses `text` only
 * for widget requests and `message` only for command requests. Real plugins should
 * parse the UTF-8 request JSON; see plugins/howlongtobeat. */
static int32_t __cdecl invoke(const char* json, uint32_t length, LaunchmeEmitV1 emit, void* context) {
    static const char response[] = "{\"text\":\"Ready\",\"message\":\"Hello from a plugin\"}";
    if (!json || !emit || length > LAUNCHME_PLUGIN_MAX_JSON) return 1;
    emit(context, response, (uint32_t)strlen(response));
    return 0;
}
static const LaunchmePluginV1 api = {sizeof(LaunchmePluginV1), LAUNCHME_PLUGIN_ABI, invoke};
const LaunchmePluginV1* __cdecl launchme_plugin_v1(uint32_t host_abi) {
    return host_abi == LAUNCHME_PLUGIN_ABI ? &api : 0;
}
