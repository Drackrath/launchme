#include "proc_fs.hpp"

#include <charconv>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace proc_fs {
namespace {
std::optional<std::string> read(const std::string& file, size_t limit) {
    const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string text;
    char buffer[4096];
    for (;;) {
        const ssize_t got = ::read(fd, buffer, sizeof(buffer));
        if (got < 0) { ::close(fd); return {}; }
        if (got == 0) break;
        text.append(buffer, static_cast<size_t>(got));
        if (text.size() > limit) break;
    }
    ::close(fd);
    return text;
}
std::string folder(uint32_t pid) { return "/proc/" + std::to_string(pid); }
bool wineLoader(const std::string& name) {
    return name == "wine" || name == "wine64" || name == "wine-preloader" || name == "wine64-preloader";
}
}

std::optional<Stat> stat(uint32_t pid) {
    const auto text = read(folder(pid) + "/stat", 4096);
    // The name sits in parentheses and may itself contain ") ": the last one ends it.
    const auto close = text ? text->rfind(')') : std::string::npos;
    if (close == std::string::npos || close + 2 >= text->size()) return {};
    Stat result;
    result.pid = pid;
    result.state = (*text)[close + 2];
    // Fields after the state: ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt
    // utime stime cutime cstime priority nice num_threads itrealvalue starttime.
    const char* at = text->data() + close + 3;
    const char* end = text->data() + text->size();
    uint64_t fields[19]{};
    for (auto& field : fields) {
        while (at < end && *at == ' ') ++at;
        const bool negative = at < end && *at == '-';
        if (negative) ++at;
        const auto parsed = std::from_chars(at, end, field);
        if (parsed.ec != std::errc{}) return {};
        if (negative) field = 0;
        at = parsed.ptr;
    }
    result.parent = static_cast<uint32_t>(fields[0]);
    result.group = static_cast<uint32_t>(fields[1]);
    result.session = static_cast<uint32_t>(fields[2]);
    result.start = fields[18];
    return result;
}

std::vector<uint32_t> processes() {
    std::vector<uint32_t> result;
    DIR* directory = ::opendir("/proc");
    if (!directory) return result;
    while (const dirent* entry = ::readdir(directory)) {
        uint32_t pid = 0;
        const std::string_view name(entry->d_name);
        const auto parsed = std::from_chars(name.data(), name.data() + name.size(), pid);
        if (parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() && pid) result.push_back(pid);
    }
    ::closedir(directory);
    return result;
}

std::optional<uint32_t> owner(uint32_t pid) {
    struct stat info{};
    if (::stat(folder(pid).c_str(), &info) != 0) return {};
    return static_cast<uint32_t>(info.st_uid);
}

std::optional<std::filesystem::path> image(uint32_t pid) {
    std::error_code error;
    auto target = std::filesystem::read_symlink(folder(pid) + "/exe", error).string();
    if (error || target.empty()) return {};
    // A program replaced on disk while it runs.
    if (target.ends_with(" (deleted)")) target.resize(target.size() - 10);
    std::filesystem::path path(target);
    if (!wineLoader(path.filename().string())) return path;
    const auto arguments = commandLine(pid);
    if (!arguments || arguments->empty() || (*arguments)[0].empty()) return path;
    auto program = (*arguments)[0];
    if (program.front() == '/') return std::filesystem::path(program);
    if (program.size() < 3 || program[1] != ':' || program[2] != '\\') return path;
    if (program[0] == 'Z' || program[0] == 'z') {
        program.erase(0, 2);
        for (auto& ch : program) if (ch == '\\') ch = '/';
    }
    return std::filesystem::path(program);
}

std::optional<std::vector<std::string>> commandLine(uint32_t pid) {
    const auto text = read(folder(pid) + "/cmdline", 256 * 1024);
    if (!text) return {};
    std::vector<std::string> result;
    for (size_t at = 0; at < text->size();) {
        const auto end = text->find('\0', at);
        result.push_back(text->substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return result;
}

std::string name(uint32_t pid) {
    auto text = read(folder(pid) + "/comm", 64).value_or(std::string{});
    while (!text.empty() && text.back() == '\n') text.pop_back();
    return text;
}
}
