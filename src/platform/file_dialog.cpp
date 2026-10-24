#include "file_dialog.hpp"
#include "../i18n/i18n.hpp"

#include <shobjidl.h>
#include <wrl/client.h>

std::filesystem::path pickPluginPackage(HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | FOS_DONTADDTORECENT);
    dialog->SetTitle(tr(L"Zainstaluj plugin z ZIP"));
    const COMDLG_FILTERSPEC types[]{{L"ZIP", L"*.zip"}};
    dialog->SetFileTypes(1, types);
    if (FAILED(dialog->Show(owner))) return {};
    Microsoft::WRL::ComPtr<IShellItem> item; PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    std::filesystem::path result(path); CoTaskMemFree(path); return result;
}

std::filesystem::path pickGamesFolder(HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | FOS_PICKFOLDERS);
    dialog->SetTitle(tr(L"Wybierz katalog z grami DRM-free"));
    if (FAILED(dialog->Show(owner))) return {};
    Microsoft::WRL::ComPtr<IShellItem> item; PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    std::filesystem::path result(path); CoTaskMemFree(path); return result;
}

std::filesystem::path pickArtworkImage(HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | FOS_DONTADDTORECENT);
    dialog->SetTitle(tr(L"Wybierz grafikę"));
    const std::wstring images = tr(L"Obrazy PNG i JPEG");
    const COMDLG_FILTERSPEC types[]{{images.c_str(), L"*.png;*.jpg;*.jpeg"}};
    dialog->SetFileTypes(1, types);
    if (FAILED(dialog->Show(owner))) return {};
    Microsoft::WRL::ComPtr<IShellItem> item; PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    std::filesystem::path result(path); CoTaskMemFree(path); return result;
}

std::filesystem::path pickGamePath(HWND owner, bool folder, const std::filesystem::path& initial) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT;
    options |= folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST | FOS_NODEREFERENCELINKS;
    dialog->SetOptions(options);
    dialog->SetTitle(folder ? tr(L"Wybierz folder roboczy") : tr(L"Wybierz plik gry"));
    const std::wstring programs = tr(L"Programy i skróty"), everything = tr(L"Wszystkie pliki");
    if (!folder) {
        const COMDLG_FILTERSPEC types[]{{programs.c_str(), L"*.exe;*.lnk;*.bat;*.cmd;*.url"}, {everything.c_str(), L"*.*"}};
        dialog->SetFileTypes(2, types);
        dialog->SetFileTypeIndex(1);
    }
    std::error_code error;
    if (!initial.empty() && std::filesystem::is_directory(initial, error)) {
        Microsoft::WRL::ComPtr<IShellItem> start;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(start.GetAddressOf())))) dialog->SetFolder(start.Get());
    }
    if (FAILED(dialog->Show(owner))) return {};
    Microsoft::WRL::ComPtr<IShellItem> result;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(result.GetAddressOf())) || FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    std::filesystem::path selected(path);
    CoTaskMemFree(path);
    return selected;
}

namespace {
std::filesystem::path pickFolder(HWND owner, const std::filesystem::path& initial, const wchar_t* title) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | FOS_PICKFOLDERS);
    dialog->SetTitle(title);
    std::error_code error;
    if (!initial.empty() && std::filesystem::is_directory(initial, error)) {
        Microsoft::WRL::ComPtr<IShellItem> start;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(start.GetAddressOf())))) dialog->SetFolder(start.Get());
    }
    if (FAILED(dialog->Show(owner))) return {};
    Microsoft::WRL::ComPtr<IShellItem> item; PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    std::filesystem::path result(path); CoTaskMemFree(path); return result;
}
}

std::filesystem::path pickVideoFolder(HWND owner, const std::filesystem::path& initial) {
    return pickFolder(owner, initial, tr(L"Wybierz folder filmów"));
}

std::filesystem::path pickSoundFolder(HWND owner, const std::filesystem::path& initial) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | FOS_PICKFOLDERS);
    dialog->SetTitle(tr(L"Wybierz folder dźwięków"));
    std::error_code error;
    if (!initial.empty() && std::filesystem::is_directory(initial, error)) {
        Microsoft::WRL::ComPtr<IShellItem> start;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(start.GetAddressOf())))) dialog->SetFolder(start.Get());
    }
    if (FAILED(dialog->Show(owner))) return {};
    Microsoft::WRL::ComPtr<IShellItem> item; PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    std::filesystem::path result(path); CoTaskMemFree(path); return result;
}
std::filesystem::path pickPath(HWND owner, bool folder, const std::wstring& title, const std::vector<std::wstring>& extensions,
                               const std::wstring& filter_label, bool all_files, const std::filesystem::path& initial) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT;
    options |= folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST | FOS_NODEREFERENCELINKS;
    dialog->SetOptions(options);
    if (!title.empty()) dialog->SetTitle(title.c_str());
    // Its types: the requested extensions ("*.png;*.jpg"), then every file when offered or needed.
    std::wstring pattern;
    for (const auto& extension : extensions) pattern += (pattern.empty() ? L"*" : L";*") + extension;
    const std::wstring label = filter_label.empty() ? pattern : filter_label, everything = tr(L"Wszystkie pliki");
    if (!folder) {
        std::vector<COMDLG_FILTERSPEC> types;
        if (!pattern.empty()) types.push_back({label.c_str(), pattern.c_str()});
        if (pattern.empty() || all_files) types.push_back({everything.c_str(), L"*.*"});
        dialog->SetFileTypes(static_cast<UINT>(types.size()), types.data());
    }
    // The start: the folder itself, or the folder of the given file (that file preselected).
    std::error_code error;
    std::filesystem::path start = initial;
    if (!start.empty() && !std::filesystem::is_directory(start, error)) {
        if (!folder && std::filesystem::is_regular_file(start, error)) dialog->SetFileName(start.filename().c_str());
        start = start.parent_path();
    }
    if (!start.empty() && std::filesystem::is_directory(start, error)) {
        Microsoft::WRL::ComPtr<IShellItem> place;
        if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(place.GetAddressOf())))) dialog->SetFolder(place.Get());
    }
    if (FAILED(dialog->Show(owner))) return {};
    Microsoft::WRL::ComPtr<IShellItem> item; PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    std::filesystem::path result(path); CoTaskMemFree(path); return result;
}