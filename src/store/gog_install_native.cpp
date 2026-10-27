#include "gog_install.hpp"
#include "../platform/hashing.hpp"
#include "../net/http_download.hpp"
#include "../net/json.hpp"
#include "../model/key_codec.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"
#include "../platform/atomic_file.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <objbase.h>
#include <bcrypt.h>
#include <shellapi.h>
#endif
#include <array>
#include <fstream>
#include <vector>

namespace gog {
namespace {
bool checksum(const std::filesystem::path& file, const std::string& expected, const InstallCancel& cancel) {
    const auto digest = hashing::fileHex(hashing::Algorithm::md5, file, cancel);
    return !digest.empty() && !cancel() && digest == expected;
}
InstallError execute(const std::filesystem::path& file, const InstallRequest& request, const InstallCancel& cancel) {
    if (cancel()) return InstallError::none;
    const auto args = silentInstallerArguments(request.install_directory);
    if (args.empty()) return InstallError::storage;
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Com { HRESULT result; ~Com() { if (SUCCEEDED(result)) CoUninitialize(); } } initialized{com};
    const auto folder = file.parent_path();
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = reinterpret_cast<HWND>(request.owner_window);
    info.lpFile = file.c_str(); info.lpParameters = args.c_str(); info.lpDirectory = folder.c_str(); info.nShow = SW_SHOWNORMAL;
    // The installer manifest invokes Windows UAC when needed; no credentials
    // are collected by Launchme. Inno Setup owns its child processes/rollback.
    if (!ShellExecuteExW(&info) || !info.hProcess) return InstallError::installer;
    struct Process { HANDLE value; ~Process() { CloseHandle(value); } } process{info.hProcess};
    while (WaitForSingleObject(info.hProcess, 100) == WAIT_TIMEOUT) {
        // Shutdown/logout stops monitoring, never force-kills an installer.
        // Keep downloads; startup's local scan discovers any completed game.
        if (cancel()) return InstallError::none;
    }
    DWORD code = 1;
    if (!GetExitCodeProcess(info.hProcess, &code) || (code != 0 && code != 3010)) return InstallError::installer;
    return InstallError::none;
}
bool detect(const InstallRequest& request) {
    const auto file = request.install_directory / (L"goggame-" + text_encoding::wide(request.product) + L".info");
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 2 * 1024 * 1024) return false;
    std::string text(static_cast<size_t>(input.tellg()), '\0'); input.seekg(0); input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
    const auto root = json::parse(text);
    const auto id = root ? root->get("gameId") : nullptr;
    const auto tasks = root ? root->get("playTasks") : nullptr;
    if (!id || id->text != request.product || !tasks || tasks->type != json::Value::Type::array) return false;
    for (const auto& task : tasks->elements) {
        const auto type = task.get("type"), path = task.get("path"), primary = task.get("isPrimary");
        if (!type || type->stringOr() != "FileTask" || !path || !primary || !primary->flag) continue;
        const auto relative = std::filesystem::path(text_encoding::wide(path->stringOr()));
        if (relative.empty() || relative.is_absolute()) continue;
        bool parent = false; for (const auto& component : relative) parent |= component == L"..";
        std::error_code ec;
        if (!parent && std::filesystem::is_regular_file(request.install_directory / relative, ec)) {
            return atomic_file::write(userConfigDirectory() / L"installed" / L"gog" / (text_encoding::wide(request.product) + L".path"),
                text_encoding::utf8(request.install_directory.wstring()));
        }
    }
    return false;
}
}
InstallOperations nativeInstallOperations() {
    return {
        [](const InstallerPart& part, const std::filesystem::path& file, const InstallCancel& cancel, const std::function<void(uint64_t)>& progress) {
            const auto result = httpDownload(part.url, file, part.bytes, cancel, progress, installerDownloadUrl);
            return result == DownloadError::none ? InstallError::none : result == DownloadError::storage ? InstallError::storage :
                result == DownloadError::invalid_response ? InstallError::manifest : InstallError::network;
        }, checksum, execute, detect
    };
}
}
