#include "sensors.hpp"

#include <Windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sensors {

std::optional<int> celsiusFromKelvin(double kelvin) {
    if (!std::isfinite(kelvin) || kelvin <= 0) return std::nullopt;
    const double celsius = std::round(kelvin - 273.15);
    if (celsius < cpu_plausible_min || celsius > plausible_max) return std::nullopt;
    return static_cast<int>(celsius);
}

std::optional<int> celsiusFromDeci(uint64_t tenths) {
    if (!tenths || tenths > static_cast<uint64_t>(plausible_max) * 10) return std::nullopt;
    return static_cast<int>((tenths + 5) / 10);
}

std::optional<int> hottest(std::span<const std::optional<int>> values) {
    std::optional<int> result;
    for (const auto& value : values)
        if (value && (!result || *value > *result)) result = value;
    return result;
}

std::wstring format(std::optional<int> celsius) {
    return celsius ? std::to_wstring(*celsius) + L" \u00B0C" : std::wstring(L"\u2014");
}

namespace {
// The few D3DKMT (gdi32) declarations used here, ABI-identical to d3dkmthk.h. They are
// resolved at run time, so the launcher needs neither the DDK headers nor an import
// that older Windows builds lack.
using KmtHandle = UINT;
struct KmtAdapterInfo { KmtHandle adapter; LUID luid; ULONG sources; BOOL precise_present; };
struct KmtEnumAdapters2 { ULONG count; KmtAdapterInfo* adapters; };
struct KmtQueryAdapterInfo { KmtHandle adapter; int type; void* data; UINT size; };
struct KmtCloseAdapter { KmtHandle adapter; };
struct KmtAdapterPerfData {
    UINT32 physical_index;
    ULONGLONG memory_frequency, max_memory_frequency, max_memory_frequency_oc, memory_bandwidth, pcie_bandwidth;
    ULONG fan_rpm, power, temperature; // temperature: tenths of °C.
    UCHAR power_state_override;
};
constexpr int kmt_adapter_type = 15;            // KMTQAITYPE_ADAPTERTYPE
constexpr int kmt_physical_adapter_count = 30;  // KMTQAITYPE_PHYSICALADAPTERCOUNT
constexpr int kmt_adapter_perf_data = 62;       // KMTQAITYPE_ADAPTERPERFDATA (WDDM 2.4+)
constexpr UINT kmt_software_device = 1u << 2;   // D3DKMT_ADAPTERTYPE::SoftwareDevice
using EnumAdapters2 = LONG(APIENTRY*)(KmtEnumAdapters2*);
using QueryAdapterInfo = LONG(APIENTRY*)(const KmtQueryAdapterInfo*);
using CloseAdapter = LONG(APIENTRY*)(const KmtCloseAdapter*);
static_assert(sizeof(KmtAdapterInfo) == 20 && sizeof(KmtAdapterPerfData) == 64);
} // namespace

struct Reader::State {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    bool pdh_tried = false;
    HMODULE gdi = nullptr;
    EnumAdapters2 enumerate = nullptr;
    QueryAdapterInfo query_adapter = nullptr;
    CloseAdapter close = nullptr;

    ~State() {
        if (query) PdhCloseQuery(query);
        if (gdi) FreeLibrary(gdi);
    }

    // Hottest ACPI thermal zone. English counter path: the localized names differ per
    // Windows display language ("Informacje o strefach termicznych" in Polish).
    std::optional<int> cpu() {
        if (!pdh_tried) {
            pdh_tried = true;
            if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) query = nullptr;
            else if (PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\Temperature", 0, &counter) != ERROR_SUCCESS) {
                PdhCloseQuery(query);
                query = nullptr;
            }
        }
        if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS) return std::nullopt;
        constexpr DWORD format = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA) || !bytes)
            return std::nullopt;
        // Items first, then their instance names; keep the buffer aligned for the doubles.
        std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items(bytes / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) + 1);
        if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, items.data()) != ERROR_SUCCESS) return std::nullopt;
        std::vector<std::optional<int>> zones;
        for (DWORD i = 0; i < count && i < items.size(); ++i) {
            const auto& value = items[i].FmtValue;
            if (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA)
                zones.push_back(celsiusFromKelvin(value.doubleValue));
        }
        return hottest(zones);
    }

    // Hottest hardware adapter, as reported by the WDDM 2.4+ driver (what Task Manager shows).
    std::optional<int> gpu() {
        if (!gdi) {
            gdi = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!gdi) return std::nullopt;
            enumerate = reinterpret_cast<EnumAdapters2>(GetProcAddress(gdi, "D3DKMTEnumAdapters2"));
            query_adapter = reinterpret_cast<QueryAdapterInfo>(GetProcAddress(gdi, "D3DKMTQueryAdapterInfo"));
            close = reinterpret_cast<CloseAdapter>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
        }
        if (!enumerate || !query_adapter || !close) return std::nullopt;
        KmtEnumAdapters2 list{};
        if (enumerate(&list) < 0 || !list.count) return std::nullopt;
        std::vector<KmtAdapterInfo> adapters(list.count);
        list.adapters = adapters.data();
        if (enumerate(&list) < 0) return std::nullopt;
        adapters.resize(std::min<size_t>(adapters.size(), list.count));
        std::vector<std::optional<int>> readings;
        for (const auto& adapter : adapters) {
            UINT type = 0;
            const KmtQueryAdapterInfo type_query{adapter.adapter, kmt_adapter_type, &type, sizeof(type)};
            if (query_adapter(&type_query) >= 0 && !(type & kmt_software_device)) {
                UINT physical = 1;
                const KmtQueryAdapterInfo count_query{adapter.adapter, kmt_physical_adapter_count, &physical, sizeof(physical)};
                if (query_adapter(&count_query) < 0 || !physical) physical = 1;
                for (UINT index = 0; index < std::min<UINT>(physical, 8); ++index) {
                    KmtAdapterPerfData data{};
                    data.physical_index = index;
                    const KmtQueryAdapterInfo perf_query{adapter.adapter, kmt_adapter_perf_data, &data, sizeof(data)};
                    if (query_adapter(&perf_query) >= 0) readings.push_back(celsiusFromDeci(data.temperature));
                }
            }
            const KmtCloseAdapter closing{adapter.adapter};
            close(&closing);
        }
        return hottest(readings);
    }
};

Reader::Reader() : state_(std::make_unique<State>()) {}
Reader::~Reader() = default;

Reading Reader::read() {
    Reading reading;
    reading.sampled = true;
    reading.cpu = state_->cpu();
    reading.gpu = state_->gpu();
    return reading;
}

} // namespace sensors
