#ifndef LAUNCHME_PLUGIN_H
#define LAUNCHME_PLUGIN_H
#include <stdint.h>

/* Windows x64 (a DLL) or Linux x86-64 (a shared object), C ABI. No CRT allocations or C++
 * objects cross this boundary.
 * All strings are UTF-8 JSON, lengths exclude NUL. See docs/plugins/API.md.
 * emit must be called exactly once, synchronously, before invoke returns.
 * The host copies the response during emit. Neither side retains pointers.
 * Calls are serialized on a worker. Plugins must stop their own work on deactivate.
 */
#define LAUNCHME_PLUGIN_ABI 1u
#define LAUNCHME_PLUGIN_MAX_JSON (2u * 1024u * 1024u)
/* The default C calling convention; spelled out only where the compiler knows __cdecl. */
#ifndef LAUNCHME_CALL
#ifdef _WIN32
#define LAUNCHME_CALL __cdecl
#else
#define LAUNCHME_CALL
#endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef void (LAUNCHME_CALL *LaunchmeEmitV1)(void* context, const char* json, uint32_t length);
typedef int32_t (LAUNCHME_CALL *LaunchmeInvokeV1)(const char* json, uint32_t length, LaunchmeEmitV1 emit, void* context);
typedef struct LaunchmePluginV1 {
    uint32_t size;
    uint32_t abi;
    LaunchmeInvokeV1 invoke;
} LaunchmePluginV1;
typedef const LaunchmePluginV1* (LAUNCHME_CALL *LaunchmeGetPluginV1)(uint32_t host_abi);
/* Export exactly this unmangled name; return NULL for unsupported host_abi. */
#ifdef _WIN32
#define LAUNCHME_PLUGIN_EXPORT __declspec(dllexport)
#else
#define LAUNCHME_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#ifdef LAUNCHME_PLUGIN_EXPORTS
LAUNCHME_PLUGIN_EXPORT
#endif
const LaunchmePluginV1* LAUNCHME_CALL launchme_plugin_v1(uint32_t host_abi);
#ifdef __cplusplus
}
#endif
#endif
