#pragma once
// One API call of a signed-in store account (GOG, Epic) made for a feature that must never see
// the account's token: plugins' store requests (src/app/plugin_achievements.cpp). The account
// worker adds the bearer token, refreshes it when it is due or refused (401) and keeps the
// rotation in its vault; the caller only gets the response.
#include "../net/http.hpp"

#include <string>

struct AccountRequest {
    std::wstring url;   // https; the caller has checked its host.
    std::string body;   // POST: a JSON body.
    bool post = false;
};
