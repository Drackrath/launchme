#pragma once
#include "../model/game.hpp"
#include "../net/http.hpp"
#include "account_request.hpp"
#include "gog_install.hpp" // Shared install progress/modal DTOs; Epic has its own backend.
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>

namespace epic {
using Cancel = std::function<bool()>;
using Http = std::function<HttpResponse(const std::wstring&, const std::wstring&, const std::string&, bool)>;
enum class Failure { none, cancelled, offline, expired, invalid, storage, configuration, rate_limit, helper, unsupported };
struct Result {
    Failure failure = Failure::none;
    uint32_t retry_after = 0;
    explicit operator bool() const { return failure == Failure::none; }
};
struct Config { std::string client, secret; bool valid() const; };
struct Tokens { std::string access, refresh, account, name; uint64_t expires = 0; };
struct Session { Tokens tokens; std::vector<Game> games; uint64_t synced_at = 0; };
uint64_t nowSeconds();
bool identifier(std::string_view);
Config prepareConfig(const std::filesystem::path&, const Http& = {});
std::wstring authorizationUrl(const Config&);
std::optional<std::string> authorizationCode(std::wstring_view);
std::string sessionJson(const Tokens&, bool include_refresh = true);
std::optional<Tokens> parseTokens(std::string_view);
class Backend {
public:
    virtual ~Backend() = default;
    virtual Result exchange(const Config&, const std::string&, Tokens&, const Cancel&) = 0;
    virtual Result verify(const Config&, Tokens&, const Cancel&) = 0;
    virtual Result library(const Config&, Session&, const Cancel&) = 0;
    // An account API call (AccountRequest): the bearer token added, refreshed when due or on 401.
    // `response` is the server's answer whatever its status.
    virtual Result call(const Config&, const AccountRequest&, Tokens&, const Cancel&, HttpResponse&) { return {Failure::configuration}; }
};
std::unique_ptr<Backend> makeBackend(Http = {});
class Vault {
public:
    virtual ~Vault() = default;
    virtual Result load(std::optional<Session>&) = 0;
    virtual bool save(const Session&) = 0;
    virtual bool clear() = 0;
};
std::unique_ptr<Vault> makeVault(const std::filesystem::path&);
enum class State { disconnected, preparing, signing_in, verifying, syncing, connected, offline, error };
enum class Command { restore, connect, complete, sync, cancel, logout };
struct Snapshot {
    State state = State::disconnected;
    Failure failure = Failure::none;
    std::string account;
    std::vector<Game> games;
    uint64_t synced_at = 0;
    std::wstring browser_url;
};
struct LaunchResult { Game game; GameAction action; Failure failure = Failure::none; };
class Runner {
public:
    virtual ~Runner() = default;
    virtual void cleanup() {}
    virtual Result install(const Tokens&, const Game&, const gog::InstallRequest&, const Cancel&, const gog::InstallProgress&) = 0;
    virtual Result launch(const Tokens&, const Game&, GameAction&, const Cancel&, bool offline) = 0;
};
std::unique_ptr<Runner> makeRunner(const std::filesystem::path& root);
class Service {
public:
    virtual ~Service() = default;
    virtual void submit(Command, std::wstring input = {}) = 0;
    virtual std::optional<Snapshot> take() = 0;
    virtual bool install(const Game&, gog::InstallRequest) = 0;
    virtual bool launch(const Game&) = 0;
    virtual void cancelOperation() = 0;
    virtual bool busy() const = 0;
    virtual std::optional<gog::InstallSnapshot> takeInstall() = 0;
    virtual std::optional<LaunchResult> takeLaunch() = 0;
    // An account API call queued for the account worker (which owns the token); its answer is
    // nullopt without a session or on a failed refresh. Shutdown breaks the promise.
    virtual std::future<std::optional<HttpResponse>> request(AccountRequest) {
        std::promise<std::optional<HttpResponse>> none; none.set_value(std::nullopt); return none.get_future();
    }
};
std::unique_ptr<Service> makeService(std::unique_ptr<Backend>, std::unique_ptr<Vault>, std::unique_ptr<Runner>,
    std::function<Config()>, std::chrono::milliseconds login_timeout = std::chrono::minutes(5));
std::wstring statusText(const Snapshot&);
std::wstring installStatusText(const gog::InstallSnapshot&);
}
