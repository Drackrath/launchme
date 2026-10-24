#pragma once
#include "http.hpp"
#include <string>
#include <vector>

namespace companion::links {
inline constexpr size_t max_links = 2, title_limit = 160, description_limit = 500, excerpt_limit = 1400, youtube_description_limit = 1000;
inline constexpr size_t page_bytes = 2 * 1024 * 1024;
struct Link {
    std::wstring url;
    std::string video_id;
};
struct Preview {
    Link link;
    std::wstring title, description, excerpt, channel, error;
    bool truncated = false;
};
using Get = std::function<HttpResponse(const std::wstring&, size_t)>;
// Pure parsers. Only explicit http(s) / www links; unique, public addresses, at most two.
std::vector<Link> find(std::wstring_view message);
std::string youtubeId(std::wstring_view url);
Preview parse(const Link& link, std::string_view body, bool plain_text = false);
std::vector<Preview> read(std::wstring_view message, const Get& get = {});
// Bounded JSON data appended to the current user turn, never trusted instructions.
std::wstring context(const std::vector<Preview>& previews);
std::wstring guidance(bool english);
// Local synthetic HTTPS document provides the embed with a real origin and referrer.
std::wstring playerUrl(std::string_view video_id);
std::wstring playerHtml(std::wstring_view url, bool english);
}
