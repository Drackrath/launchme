#ifndef LAUNCHME_PLUGIN_H
#define LAUNCHME_PLUGIN_H
#include <stdint.h>

/* Windows x64, C ABI. No CRT allocations or C++ objects cross this boundary.
 * All strings are UTF-8 JSON, lengths exclude NUL. See docs/plugins/API.md.
 * emit must be called exactly once, synchronously, before invoke returns.
 * The host copies the response during emit. Neither side retains pointers.
 * Calls are serialized on a worker. Plugins must stop their own work on deactivate.
 */
#define LAUNCHME_PLUGIN_ABI 1u
#define LAUNCHME_PLUGIN_MAX_JSON (2u * 1024u * 1024u)
#ifdef __cplusplus
extern "C" {
#endif
typedef void (__cdecl *LaunchmeEmitV1)(void* context, const char* json, uint32_t length);
typedef int32_t (__cdecl *LaunchmeInvokeV1)(const char* json, uint32_t length, LaunchmeEmitV1 emit, void* context);
typedef struct LaunchmePluginV1 {
    uint32_t size;
    uint32_t abi;
    LaunchmeInvokeV1 invoke;
} LaunchmePluginV1;
typedef const LaunchmePluginV1* (__cdecl *LaunchmeGetPluginV1)(uint32_t host_abi);
/* Export exactly this unmangled name; return NULL for unsupported host_abi. */
#ifdef LAUNCHME_PLUGIN_EXPORTS
__declspec(dllexport)
#endif
const LaunchmePluginV1* __cdecl launchme_plugin_v1(uint32_t host_abi);
#ifdef __cplusplus
}
#endif
#endif
