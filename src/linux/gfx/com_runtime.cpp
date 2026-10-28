// The COM runtime's part the launcher uses (combaseapi.h): per-thread initialisation counts,
// in-process classes for CoCreateInstance, and task memory.
#include "wic_internal.hpp"

#include <cstdlib>
#include <mutex>
#include <vector>

namespace {
// CoInitializeEx calls on this thread not yet balanced by CoUninitialize.
thread_local unsigned initialized = 0;

struct Class { CLSID clsid; lm_com::ClassFactory factory; };
struct Registry {
    std::mutex mutex;
    std::vector<Class> classes{{CLSID_WICImagingFactory, lm_wic::createImagingFactory}, {CLSID_WICImagingFactory2, lm_wic::createImagingFactory}};
};
Registry& registry() {
    static Registry instance;
    return instance;
}
} // namespace

HRESULT CoInitializeEx(LPVOID, DWORD) { return initialized++ ? S_FALSE : S_OK; }

void CoUninitialize() {
    if (initialized) --initialized;
}

HRESULT CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD, REFIID riid, LPVOID* object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    lm_com::ClassFactory factory = nullptr;
    {
        auto& known = registry();
        std::lock_guard lock(known.mutex);
        for (const auto& entry : known.classes)
            if (entry.clsid == clsid) factory = entry.factory;
    }
    return factory ? factory(riid, object) : REGDB_E_CLASSNOTREG;
}

LPVOID CoTaskMemAlloc(SIZE_T size) { return std::malloc(size ? size : 1); }
void CoTaskMemFree(LPVOID memory) { std::free(memory); }

namespace lm_com {
void registerClass(const CLSID& clsid, ClassFactory factory) {
    auto& known = registry();
    std::lock_guard lock(known.mutex);
    for (auto& entry : known.classes)
        if (entry.clsid == clsid) { entry.factory = factory; return; }
    known.classes.push_back({clsid, factory});
}
} // namespace lm_com
