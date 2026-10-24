#include "gog_account.hpp"
#include "../net/json.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"
#include <iomanip>
#include <sstream>

namespace gog {
namespace {
constexpr auto redirect = "https://embed.gog.com/on_login_success?origin=client";
// Data-only, immutable upstream revision. No downloaded script is executed and
// no third-party client credential is compiled into the launcher or repository.
constexpr auto profile_url = L"https://raw.githubusercontent.com/mubaraknumann/unifideck/2423d32a8a22909cd3da39960eed55ad28384858/defaults/config.json";
std::string field(const json::Value& value, const char* key) { const auto item = value.get(key); return item ? item->stringOr() : std::string{}; }
}
Config parsePublicClientProfile(std::string_view text) {
    const auto root = json::parse(text); const auto stores = root ? root->get("stores") : nullptr; const auto gog = stores ? stores->get("gog") : nullptr;
    if (!gog || field(*gog, "auth_url") != "https://auth.gog.com/auth" || field(*gog, "token_url") != "https://auth.gog.com/token" || field(*gog, "redirect_uri") != redirect) return {};
    Config config{field(*gog, "client_id"), field(*gog, "client_secret"), redirect, true};
    if (!config.valid() || config.client_id.find_first_not_of("0123456789") != std::string::npos || config.client_secret.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return {};
    return config;
}
Config prepareClientProfile(const std::filesystem::path& file, const Http& get) {
    if (auto cached = protected_file::read(file, 16 * 1024); cached && !cached->empty()) {
        std::istringstream input(*cached); SecureZeroMemory(cached->data(), cached->size());
        Config config; std::string header;
        if (input >> header >> std::quoted(config.client_id) >> std::quoted(config.client_secret) >> std::quoted(config.redirect_uri) &&
            header == "launchme-gog-client-v1" && config.valid() && config.redirect_uri == redirect) {
            config.legacy_manual_callback = true; return config;
        }
    }
    const auto response = get(profile_url, L"Accept: application/json\r\n", 128 * 1024);
    if (response.status != 200) return {};
    const auto config = parsePublicClientProfile({reinterpret_cast<const char*>(response.body.data()), response.body.size()});
    if (!config.valid()) return {};
    std::ostringstream output;
    output << "launchme-gog-client-v1\n" << std::quoted(config.client_id) << ' ' << std::quoted(config.client_secret) << ' ' << std::quoted(config.redirect_uri);
    auto text = output.str(); const bool saved = protected_file::write(file, text, L"Launchme GOG client profile");
    SecureZeroMemory(text.data(), text.size());
    return saved ? config : Config{};
}
Config loadConfig() {
    Config explicit_config{text_encoding::utf8(userSecret(L"GOG_CLIENT_ID", L"gog_client_id.txt")),
        text_encoding::utf8(userSecret(L"GOG_CLIENT_SECRET", L"gog_client_secret.txt")),
        text_encoding::utf8(userSecret(L"GOG_REDIRECT_URI", L"gog_redirect_uri.txt"))};
    if (!explicit_config.client_id.empty() || !explicit_config.client_secret.empty() || !explicit_config.redirect_uri.empty()) return explicit_config;
    return prepareClientProfile(userConfigDirectory() / L"accounts" / L"gog-client.bin");
}
}
