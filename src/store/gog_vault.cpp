#include "gog_account.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"
#include <iomanip>
#include <set>
#include <sstream>

namespace gog {
namespace {
bool safe(std::string_view value, size_t maximum) {
    return !value.empty() && value.size() <= maximum && value.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
bool decimal(std::string_view value) {
    return safe(value, 96) && value.front() != '0' && value.find_first_not_of("0123456789") == std::string::npos;
}
bool token(std::string_view value) {
    return safe(value, 8192) && std::all_of(value.begin(), value.end(), [](unsigned char c) { return c > 32 && c < 127; });
}
bool valid(const Session& session) {
    if (!token(session.tokens.access) || !token(session.tokens.refresh) || !decimal(session.tokens.user) ||
        !token(session.tokens.client) || !session.tokens.expires || session.games.size() > 10000 ||
        (!session.synced_at && !session.games.empty())) return false;
    std::set<std::string> seen;
    for (const auto& game : session.games)
        if (game.source != GameSource::gog || game.kind != GameKind::store || game.installed || game.app_id ||
            !decimal(game.source_id) || !seen.insert(game.source_id).second || !safe(text_encoding::utf8(game.name), 2048) ||
            static_cast<int>(game.visibility) < 0 || static_cast<int>(game.visibility) > 2) return false;
    return true;
}
class DpapiVault final : public Vault {
    std::filesystem::path file_;
public:
    explicit DpapiVault(std::filesystem::path file) : file_(std::move(file)) {}
    Result load(std::optional<Session>& out) override {
        out.reset();
        std::error_code error;
        if (!std::filesystem::exists(file_, error)) return error ? Result{Failure::storage} : Result{};
        auto text = protected_file::read(file_, 32 * 1024 * 1024);
        if (!text) return {Failure::storage};
        if (text->empty()) return {}; // Durable logout tombstone.
        std::istringstream data(*text); SecureZeroMemory(text->data(), text->size());
        data.imbue(std::locale::classic());
        Session session; std::string header; size_t count = 0;
        if (!(data >> header) || header != "launchme-gog-v1" ||
            !(data >> std::quoted(session.tokens.client) >> std::quoted(session.tokens.user) >> std::quoted(session.tokens.access) >>
                std::quoted(session.tokens.refresh) >> session.tokens.expires >> session.synced_at >> count) || count > 10000) return {Failure::storage};
        for (size_t i = 0; i < count; ++i) {
            Game game; std::string name; int visibility = 0;
            if (!(data >> std::quoted(game.source_id) >> std::quoted(name) >> visibility) || visibility < 0 || visibility > 2) return {Failure::storage};
            game.source = GameSource::gog; game.kind = GameKind::store; game.installed = false;
            game.name = text_encoding::wide(name); game.visibility = static_cast<GameVisibility>(visibility);
            session.games.push_back(std::move(game));
        }
        data >> std::ws;
        if (!data.eof() || !valid(session)) return {Failure::storage};
        out = std::move(session); return {};
    }
    bool save(const Session& session) override {
        if (!valid(session)) return false;
        std::ostringstream data; data.imbue(std::locale::classic());
        data << "launchme-gog-v1\n" << std::quoted(session.tokens.client) << ' ' << std::quoted(session.tokens.user) << ' ' <<
            std::quoted(session.tokens.access) << ' ' << std::quoted(session.tokens.refresh) << ' ' << session.tokens.expires << ' ' <<
            session.synced_at << ' ' << session.games.size() << '\n';
        for (const auto& game : session.games) data << std::quoted(game.source_id) << ' ' << std::quoted(text_encoding::utf8(game.name)) << ' ' << static_cast<int>(game.visibility) << '\n';
        auto text = data.str();
        const bool saved = protected_file::write(file_, text, L"Launchme GOG account");
        SecureZeroMemory(text.data(), text.size());
        return saved;
    }
    bool clear() override {
        // An atomically replaced tombstone prevents an old session from being
        // resurrected if file deletion is denied after logout.
        if (!atomic_file::write(file_, std::string_view{})) return false;
        std::error_code error; std::filesystem::remove(file_, error);
        return true;
    }
};
}
std::unique_ptr<Vault> makeVault(const std::filesystem::path& file) { return std::make_unique<DpapiVault>(file); }
}
