#include "epic_account.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"
#include <iomanip>
#include <set>
#include <sstream>

namespace epic {
namespace {
bool valid(const Session& s) {
    if (!identifier(s.tokens.account) || s.tokens.access.empty() || s.tokens.refresh.empty() ||
        s.tokens.access.size() > 8192 || s.tokens.refresh.size() > 8192 || !s.tokens.expires || s.games.size() > 10000 || (!s.synced_at && !s.games.empty())) return false;
    std::set<std::string> ids;
    for (const auto& g : s.games)
        if (g.source != GameSource::epic || g.kind != GameKind::store || g.installed || !identifier(g.source_id) ||
            !identifier(g.epic_namespace) || !identifier(g.epic_catalog) || g.name.empty() || g.name.size() > 2048 || g.epic_partner.size() > 256 ||
            !ids.insert(g.source_id).second || static_cast<unsigned>(g.visibility) > 2) return false;
    return true;
}
class DpapiVault final : public Vault {
    std::filesystem::path file_;
public:
    explicit DpapiVault(std::filesystem::path p) : file_(std::move(p)) {}
    Result load(std::optional<Session>& out) override {
        out.reset(); std::error_code ec;
        if (!std::filesystem::exists(file_, ec)) return ec ? Result{Failure::storage} : Result{};
        auto text = protected_file::read(file_, 32 * 1024 * 1024);
        if (!text) return {Failure::storage};
        if (text->empty()) return {};
        std::istringstream data(*text); SecureZeroMemory(text->data(), text->size()); data.imbue(std::locale::classic());
        Session s; std::string header; size_t count = 0;
        if (!(data >> header) || header != "launchme-epic-v1" ||
            !(data >> std::quoted(s.tokens.access) >> std::quoted(s.tokens.refresh) >> std::quoted(s.tokens.account) >> std::quoted(s.tokens.name) >> s.tokens.expires >> s.synced_at >> count) || count > 10000) return {Failure::storage};
        for (size_t i = 0; i < count; ++i) {
            Game g; std::string name; int visibility = 0;
            if (!(data >> std::quoted(g.source_id) >> std::quoted(name) >> std::quoted(g.epic_namespace) >> std::quoted(g.epic_catalog) >> std::quoted(g.epic_partner) >> visibility) || visibility < 0 || visibility > 2) return {Failure::storage};
            g.source = GameSource::epic; g.kind = GameKind::store; g.installed = false;
            g.name = text_encoding::wide(name); g.visibility = static_cast<GameVisibility>(visibility); s.games.push_back(std::move(g));
        }
        data >> std::ws;
        if (!data.eof() || !valid(s)) return {Failure::storage};
        out = std::move(s); return {};
    }
    bool save(const Session& s) override {
        if (!valid(s)) return false;
        std::ostringstream data; data.imbue(std::locale::classic());
        data << "launchme-epic-v1\n" << std::quoted(s.tokens.access) << ' ' << std::quoted(s.tokens.refresh) << ' ' << std::quoted(s.tokens.account) << ' ' <<
            std::quoted(s.tokens.name) << ' ' << s.tokens.expires << ' ' << s.synced_at << ' ' << s.games.size() << '\n';
        for (const auto& g : s.games) data << std::quoted(g.source_id) << ' ' << std::quoted(text_encoding::utf8(g.name)) << ' ' << std::quoted(g.epic_namespace) << ' ' <<
            std::quoted(g.epic_catalog) << ' ' << std::quoted(g.epic_partner) << ' ' << static_cast<int>(g.visibility) << '\n';
        auto text = data.str(); const auto saved = protected_file::write(file_, text, L"Launchme Epic account");
        SecureZeroMemory(text.data(), text.size()); return saved;
    }
    bool clear() override {
        if (!atomic_file::write(file_, std::string_view{})) return false;
        std::error_code ec; std::filesystem::remove(file_, ec); return true;
    }
};
}
std::unique_ptr<Vault> makeVault(const std::filesystem::path& file) { return std::make_unique<DpapiVault>(file); }
}
