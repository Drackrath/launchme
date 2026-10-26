#include "achievements.hpp"
#include "../../src/platform/text_encoding.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <charconv>

namespace achievements {
using namespace plugins::wire;
namespace {
int64_t daysFrom(int y, unsigned m, unsigned d) { // Howard Hinnant's days_from_civil.
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
bool digits(std::string_view text, size_t at, size_t count, int& out) {
    if (at + count > text.size()) return false;
    const auto r = std::from_chars(text.data() + at, text.data() + at + count, out);
    return r.ec == std::errc{} && r.ptr == text.data() + at + count;
}
json::Value number(double value) { json::Value v; v.type = json::Value::Type::number; v.numeric = value; return v; }
}

GameInfo gameInfo(const json::Value& game) {
    GameInfo out;
    out.key = text(game, "key"); out.name = text(game, "name"); out.source = text(game, "source");
    out.source_id = text(game, "sourceId"); out.platform = text(game, "platform"); out.epic_namespace = text(game, "epicNamespace");
    out.rom = text_encoding::wideLenient(text(game, "rom"));
    out.install_directory = text_encoding::wideLenient(text(game, "installDirectory"));
    out.executable = text_encoding::wideLenient(text(game, "executable"));
    out.shortcut = field(game, "shortcut").type == json::Value::Type::boolean && field(game, "shortcut").flag;
    return out;
}

json::Value listJson(const List& list) {
    auto out = object();
    out.properties["source"] = string(list.source);
    auto& items = out.properties["items"]; items.type = json::Value::Type::array;
    for (const auto& item : list.items) {
        auto entry = object();
        entry.properties["id"] = string(item.id);
        entry.properties["name"] = string(item.name);
        entry.properties["description"] = string(item.description);
        if (!item.icon.empty()) entry.properties["icon"] = string(item.icon);
        entry.properties["achieved"] = boolean(item.achieved);
        entry.properties["hidden"] = boolean(item.hidden);
        if (item.achieved && item.unlocked_at) entry.properties["unlockedAt"] = number(static_cast<double>(item.unlocked_at));
        if (item.percent >= 0) entry.properties["percent"] = number(std::min(100.0, item.percent));
        items.elements.push_back(std::move(entry));
    }
    auto wrapped = object();
    wrapped.properties["achievements"] = std::move(out);
    return wrapped;
}

uint64_t parseIsoTime(std::string_view text) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!digits(text, 0, 4, year) || text.size() < 19 || text[4] != '-' || !digits(text, 5, 2, month) || text[7] != '-' || !digits(text, 8, 2, day) ||
        (text[10] != 'T' && text[10] != ' ') || !digits(text, 11, 2, hour) || text[13] != ':' || !digits(text, 14, 2, minute) || text[16] != ':' ||
        !digits(text, 17, 2, second) || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60 || year < 1971) return 0;
    size_t at = 19;
    if (at < text.size() && text[at] == '.') { ++at; while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at; }
    int64_t offset = 0;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        const int sign = text[at] == '-' ? -1 : 1;
        int h = 0, m = 0;
        if (!digits(text, at + 1, 2, h)) return 0;
        const size_t minutes = at + 3 < text.size() && text[at + 3] == ':' ? at + 4 : at + 3;
        if (minutes < text.size()) digits(text, minutes, 2, m);
        offset = sign * (h * 3600 + m * 60);
    }
    const int64_t seconds = daysFrom(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 + minute * 60 + second - offset;
    return seconds > 0 ? static_cast<uint64_t>(seconds) : 0;
}

std::string md5Hex(const std::vector<uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0) return {};
    uint8_t digest[16]{};
    const bool ok = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), digest, sizeof(digest)) >= 0;
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (const auto b : digest) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}
}
