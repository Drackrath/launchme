#pragma once
#include "web_import.hpp"
#include "../net/http.hpp"
#include <functional>
#include <string_view>

// EA app library imported like the Epic sign-in: the user signs in to EA in the browser at
// login_url, whose final page is a JSON with a short-lived access token, and pastes it into
// Launchme. The token is used at once for EA's GraphQL service (the one the EA app and the
// ea.com library use) and then discarded: only the list of owned games is kept.
// Flow and queries follow lutris/services/ea_app.py @3547bc0.
namespace ea {
inline constexpr const wchar_t* login_url =
    L"https://accounts.ea.com/connect/auth?client_id=ORIGIN_JS_SDK&response_type=token&redirect_uri=nucleus:rest";

enum class Failure { none, input, signed_out, expired, network, response };
struct Import {
    Failure failure = Failure::none;
    web_import::Snapshot snapshot;
};
// POST with JSON body; injectable for tests. Never follows redirects (bearer token).
using Post = std::function<HttpResponse(const std::wstring& url, const std::wstring& headers, const std::string& body)>;

// The access token from the pasted page (whole JSON or the bare token); empty when absent.
std::string accessToken(std::string_view pasted);
bool signedOut(std::string_view pasted); // The page reported login_required.
Import importLibrary(std::string_view pasted, const Post& post);
std::wstring failureText(Failure failure);
}
