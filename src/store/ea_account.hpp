#pragma once
#include "web_import.hpp"
#include "../net/http.hpp"
#include <functional>
#include <string_view>

// EA library imported without the EA app, like the Epic sign-in: the user signs in on ea.com in
// the browser (login_url), then token_url silently issues a short-lived access token for that
// session as a JSON page, which the user pastes into Launchme. The token is used at once for EA's
// GraphQL service (the one the EA app and the ea.com library use) and then discarded: only the
// list of owned games is kept. Signing in at token_url itself (no prompt=none) is refused by EA
// ("Your request cannot be completed. Service limitations apply."), so the two stay apart.
// Flow and queries follow lutris/services/ea_app.py (master, 2026: www.ea.com/login, then
// ORIGIN_JS_SDK with prompt=none).
namespace ea {
inline constexpr const wchar_t* login_url = L"https://www.ea.com/login";
inline constexpr const wchar_t* token_url =
    L"https://accounts.ea.com/connect/auth?client_id=ORIGIN_JS_SDK&response_type=token&redirect_uri=nucleus:rest&prompt=none";

enum class Failure { none, input, signed_out, expired, network, response };
struct Import {
    Failure failure = Failure::none;
    web_import::Snapshot snapshot;
};
// POST with JSON body; injectable for tests. Never follows redirects (bearer token).
using Post = std::function<HttpResponse(const std::wstring& url, const std::wstring& headers, const std::string& body)>;

// The access token from what was pasted: the JSON page (also with the browser's own text around
// it), an address carrying access_token= (ea.com's sign-in redirect) or the bare token; empty
// when absent.
std::string accessToken(std::string_view pasted);
bool signedOut(std::string_view pasted); // The page reported login_required (JSON or address).
Import importLibrary(std::string_view pasted, const Post& post);
std::wstring failureText(Failure failure);
}
