// Linux only: starting programs (posix_spawn), the launch job (a session), Steam's registry.vdf
// and the Linux side of the store helpers. Only shell scripts in a temporary folder are started;
// no link, file or folder is handed to the desktop (xdg-open), and the user's Steam is untouched.
#include "../src/library/steam_accounts.hpp"
#include "../src/library/steam_registry.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/command_line.hpp"
#include "../src/platform/game_activity.hpp"
#include "../src/platform/launch.hpp"
#include "../src/platform/proc_fs.hpp"
#include "../src/platform/process_spawn.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/store/gog_install.hpp"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>

namespace {
int failures = 0;
void check(bool condition, const char* what) {
    if (!condition) { ++failures; std::cerr << "FAILED: " << what << '\n'; }
}
void writeScript(const std::filesystem::path& file, const std::string& body, bool executable) {
    std::ofstream(file, std::ios::binary) << "#!/bin/sh\n" << body;
    std::filesystem::permissions(file, executable ? std::filesystem::perms::owner_all : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
}
// The processes of `session` that are still running.
std::vector<uint32_t> members(uint32_t session) {
    std::vector<uint32_t> result;
    for (const auto pid : proc_fs::processes())
        if (const auto stat = proc_fs::stat(pid); stat && !stat->ended() && stat->session == session) result.push_back(pid);
    return result;
}
template<class Condition> bool waitFor(Condition condition) {
    for (int i = 0; i < 300; ++i) { if (condition()) return true; Sleep(10); }
    return condition();
}

void arguments() {
    using process_spawn::splitArguments;
    check(splitArguments("-silent -applaunch 10 +map \"de dust\"") == std::vector<std::string>{"-silent", "-applaunch", "10", "+map", "de dust"}, "plain split");
    check(splitArguments("  ") .empty() && splitArguments("\"\"") == std::vector<std::string>{""}, "empty arguments");
    check(splitArguments("a\\\\b \"c\\\"d\" e\\\\\\\"f") == std::vector<std::string>{"a\\\\b", "c\"d", "e\\\"f"}, "backslash rules");
    for (const auto* text : {L"plain", L"with space", L"quote\"inside", L"trailing\\", L"C:\\Games\\", L"two\\\\\"x"}) {
        const auto line = text_encoding::utf8(command_line::quoteArgument(text) + L' ' + command_line::quoteArgument(L"next"));
        check(splitArguments(line) == std::vector<std::string>{text_encoding::utf8(text), "next"}, "quoteArgument round trip");
    }
}

void launches(const std::filesystem::path& root) {
    // A script that leaves a program running and exits: the session still holds the game.
    const auto script = root / "game.sh";
    const auto marker = root / "arguments.txt";
    writeScript(script, "printf '%s|' \"$@\" > \"" + marker.string() + "\"\nsleep 30 &\nexit 0\n", true);
    GameAction action{script.wstring(), L"one \"two words\"", root, ActionKind::play};
    check(executeGameAction(nullptr, action) == 33, "script started");
    const auto process = takeLaunchedProcess();
    check(process > 0 && takeLaunchedProcess() == 0, "launched process reported once");
    const auto job = launchJob(process);
    check(job == process, "the job is the program's session");
    check(waitFor([&] { std::ifstream in(marker); std::string text; std::getline(in, text); return text == "one|two words|"; }), "arguments arrive split");
    // The script itself has exited (and was reaped); its sleep keeps the job running.
    check(waitFor([&] { return !proc_fs::stat(static_cast<uint32_t>(process)); }), "leader reaped, no zombie");
    const auto left = members(static_cast<uint32_t>(job));
    check(left.size() == 1 && proc_fs::name(left.front()) == "sleep", "the program the script started stays in the session");
    check(readLaunchJob(job) == GameActivity::running, "job running while its program runs");
    for (const auto pid : left) ::kill(static_cast<pid_t>(pid), SIGKILL);
    check(waitFor([&] { return readLaunchJob(job) == GameActivity::stopped; }), "job stopped once the session is empty");
    check(readLaunchJob(0) == GameActivity::unknown, "no job, no answer");

    // Without its executable bit a .sh still runs (as Windows runs a .bat).
    const auto plain = root / "plain.sh";
    const auto plain_marker = root / "plain.txt";
    writeScript(plain, "echo ran > \"" + plain_marker.string() + "\"\n", false);
    check(executeGameAction(nullptr, {plain.wstring(), {}, {}, ActionKind::play}) == 33, "non-executable shell script started");
    check(waitFor([&] { return std::filesystem::exists(plain_marker); }), "non-executable shell script ran");
    takeLaunchedProcess();

    check(executeGameAction(nullptr, {(root / "missing.sh").wstring(), {}, {}, ActionKind::play}) == launch_error::file_missing, "missing file");
    check(executeGameAction(nullptr, {(root / "gone" / "game.sh").wstring(), {}, {}, ActionKind::play}) == launch_error::path_missing, "missing folder");
    check(executeGameAction(nullptr, {script.wstring(), {}, root / "gone", ActionKind::play}) == launch_error::path_missing, "missing working folder");
    std::ofstream(root / "game.exe") << "MZ";
    check(executeGameAction(nullptr, {(root / "game.exe").wstring(), {}, {}, ActionKind::play}) == launch_error::no_handler, "Windows program needs Wine");
    check(executeGameAction(nullptr, {L"launchme-no-such-program-xyz", {}, {}, ActionKind::play}) == launch_error::file_missing, "program not in PATH");
    check(executeGameAction(nullptr, {}) == launch_error::other && takeLaunchedProcess() == 0, "empty action");
}

void registry(const std::filesystem::path& root) {
    const auto file = root / "registry.vdf";
    atomic_file::write(file, "\"Registry\"\n{\n\t\"HKCU\"\n\t{\n\t\t\"Software\"\n\t\t{\n\t\t\t\"Valve\"\n\t\t\t{\n\t\t\t\t\"Steam\"\n\t\t\t\t{\n"
        "\t\t\t\t\t\"language\"\t\t\"polish\"\n\t\t\t\t\t\"RunningAppID\"\t\t\"570\"\n\t\t\t\t\t\"apps\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"570\"\n\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\t\"Running\"\t\t\"1\"\n\t\t\t\t\t\t\t\"name\"\t\t\"a \\\"quoted\\\" name\"\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n");
    check(steam_registry::number(file, {}, "runningappid") == 570u, "top-level value, any case");
    check(steam_registry::number(file, {"Apps", "570"}, "Running") == 1u, "nested value");
    check(steam_registry::value(file, {"Apps", "570"}, "name") == "a \"quoted\" name", "escaped value");
    check(!steam_registry::value(file, {"Apps", "10"}, "Running"), "missing key");
    check(steam_registry::set(file, {{"AutoLoginUser", "second"}, {"Language", "english"}}), "values written");
    check(steam_registry::value(file, {}, "AutoLoginUser") == "second" && steam_registry::value(file, {}, "language") == "english" &&
          steam_registry::value(file, {"Apps", "570"}, "name") == "a \"quoted\" name", "other content kept");
    check(!steam_registry::set(root / "none.vdf", {{"AutoLoginUser", "x"}}), "no file, no Steam");
    check(!steam_registry::parse("\"a\" { \"b\" \"c\"").has_value(), "unbalanced file rejected");

    // Signing in to another account: registry.vdf in a fake home, loginusers.vdf in a fake root.
    const auto home = root / "home";
    std::filesystem::create_directories(home / ".steam");
    std::filesystem::copy_file(file, home / ".steam" / "registry.vdf");
    const auto steam_root = root / "steam";
    std::filesystem::create_directories(steam_root / "config");
    atomic_file::write(steam_root / "config" / "loginusers.vdf", "\"users\"\n{\n\t\"76561197960265729\"\n\t{\n\t\t\"AccountName\"\t\t\"first\"\n"
        "\t\t\"RememberPassword\"\t\t\"1\"\n\t\t\"AutoLogin\"\t\t\"1\"\n\t}\n\t\"76561197960265730\"\n\t{\n\t\t\"AccountName\"\t\t\"other\"\n"
        "\t\t\"RememberPassword\"\t\t\"1\"\n\t}\n}\n");
    const std::optional<std::string> previous = std::getenv("HOME") ? std::optional<std::string>(std::getenv("HOME")) : std::nullopt;
    ::setenv("HOME", home.c_str(), 1);
    check(steam_registry::file() == home / ".steam" / "registry.vdf", "registry.vdf in the home folder");
    const auto accounts = steam_accounts::loginUsers(steam_root);
    check(accounts.size() == 2 && steam_accounts::prepareSignIn(steam_root, accounts[1]), "sign-in prepared");
    check(steam_registry::value({}, "AutoLoginUser") == "other" && steam_registry::value({}, "RememberPassword") == "1", "registry.vdf names the account");
    const auto after = steam_accounts::loginUsers(steam_root);
    check(after.size() == 2 && !after[0].auto_login && after[1].auto_login, "loginusers.vdf names the account");
    if (previous) ::setenv("HOME", previous->c_str(), 1); else ::unsetenv("HOME");
    steam_accounts::libraryFolders(steam_root); // No Steam libraries here: an empty list, no error.
}

void stores(const std::filesystem::path& root) {
    // GOG's Windows installers are not run here: nothing is downloaded or started.
    const auto operations = gog::nativeInstallOperations();
    bool called = false;
    check(operations.download({L"setup.exe", L"https://cdn.gog.com/x", std::string(32, '0'), 1}, root / "setup.exe.part", [] { return false; },
                              [&](uint64_t) { called = true; }) == gog::InstallError::unavailable && !called, "GOG download refused before any byte");
    check(operations.execute(root / "setup.exe", {}, [] { return false; }) == gog::InstallError::unavailable, "GOG installer not started");
    check(!std::filesystem::exists(root / "setup.exe.part"), "no partial file");
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / ("launchme-process-tests-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    arguments();
    launches(root);
    registry(root);
    stores(root);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    if (failures) return 1;
    std::cout << "Launches, launch jobs, registry.vdf and store helper checks passed\n";
    return 0;
}
