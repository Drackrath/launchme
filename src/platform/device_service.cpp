#include "device_service.hpp"
#include "atomic_file.hpp"
#include "image_validation.hpp"
#include "../net/http.hpp"
#include "../model/headphones.hpp"
#include "../model/controller_pictures.hpp"
#include <fstream>

// Device photos, shared by the services (device_service_windows.cpp, device_service_linux.cpp).
namespace {
bool pngImage(const std::vector<uint8_t>& bytes) {
    const auto info = image_validation::inspect(bytes);
    return info && info->png && info->width >= 64 && info->height >= 64 && info->hasTransparency();
}
}
std::vector<uint8_t> headphonePicture(const std::filesystem::path& cache) {
    const auto valid = [](const auto& bytes) {
        const auto info = image_validation::inspect(bytes);
        return info && info->width >= 64 && info->height >= 64 && info->hasTransparency();
    };
    const auto file = cache / L"ath-m50xbt2.webp";
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (input && input.tellg() > 0 && input.tellg() <= 8 * 1024 * 1024) {
        std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg())); input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (valid(bytes)) return bytes;
    }
    input.close();
    auto response = httpGet(headphones::picture_url, L"", 8 * 1024 * 1024);
    if (response.status != 200 || !valid(response.body)) return {};
    atomic_file::write(file, response.body);
    return response.body;
}
std::vector<uint8_t> controllerPicture(devices::Picture kind, const std::filesystem::path& cache) {
    const auto* photo = devices::controllerPhoto(kind);
    if (!photo) return {};
    const auto file = cache / (std::to_wstring(static_cast<int>(kind)) + L".alpha-v2.png");
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (input && input.tellg() > 0 && input.tellg() <= 8 * 1024 * 1024) {
        std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg())); input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (pngImage(bytes)) return bytes;
    }
    input.close();
    auto response = httpGet(photo->url, L"", 8 * 1024 * 1024);
    if (response.status != 200 || !pngImage(response.body)) return {};
    atomic_file::write(file, response.body);
    return response.body;
}
