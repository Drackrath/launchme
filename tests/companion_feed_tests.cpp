#include "companion_fixture.hpp"
#include "../src/net/companion_feed.hpp"
#include "../src/net/companion_chats.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/ui/companion_feed_view.hpp"
#include <iostream>

namespace {
std::vector<uint8_t> bmp(uint8_t red) {
    std::vector<uint8_t> out(58);
    const auto put = [&](size_t at, uint32_t value) { for (int i = 0; i < 4; ++i) out[at + i] = static_cast<uint8_t>(value >> (8 * i)); };
    out[0] = 'B'; out[1] = 'M'; put(2, 58); put(10, 54); put(14, 40); put(18, 1); put(22, 1);
    out[26] = 1; out[28] = 24; put(34, 4); out[56] = red;
    return out;
}
int run() {
    namespace f = companion::feed;
    const companion_fixture::Cast cast;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-feed-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    const auto source = root / L"source", folder = f::folderOf(root, "hana");
    const auto first = bmp(40), second = bmp(100), third = bmp(200);
    const auto now = companion::chats::localSeconds(2026, 10, 20, 10, 0, 0);
    constexpr int day = 20261020;
    if (!f::load(folder) || !f::due({}, day, now)) return 1;
    const std::string abc = "abc";
    if (f::fingerprint(std::span(reinterpret_cast<const uint8_t*>(abc.data()), abc.size())) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") return 2;
    // Other profiles, unpaired pictures and corrupt images are never candidates.
    if (!atomic_file::write(source / L"rin" / L"1.bmp", first) || !atomic_file::write(source / L"rin" / L"1.txt", "other profile") ||
        !atomic_file::write(source / L"hana" / L"1.bmp", first) || !atomic_file::write(source / L"hana" / L"bad.png", "\x89PNGbad") ||
        !atomic_file::write(source / L"hana" / L"bad.txt", "not an image")) return 3;
    if (f::pick(source, "hana", {}, 1)) return 4;
    if (!atomic_file::write(source / L"hana" / L"1.txt", "\xef\xbb\xbf" "Photoshoot for myself.\r\nHoping a magazine notices.")) return 5;
    const auto draft = f::pick(source, "hana", {}, 1);
    if (!draft || draft->bytes != first || draft->description != L"Photoshoot for myself.  Hoping a magazine notices.") return 6;
    // A failed/empty generation consumes nothing. An exclusive publisher also leaves the pool intact.
    if (f::publish(folder, *draft, L"", day, now)) return 7;
    auto state = f::publish(folder, *draft, L"Sesja tylko dla mnie. Może zauważy mnie jakiś magazyn?", day, now);
    if (!state || state->posts.size() != 1 || !f::image(folder, draft->id) || f::image(folder, "../outside")) return 8;
    if (f::due(*state, day, now + 3600) || !f::due(*state, day, now + f::publication_gap) || f::due(*state, 20261019, now - 86400)) return 9;
    state = f::load(folder);
    if (!state || state->posts[0].text.find(L"magazyn") == std::wstring::npos) return 10;
    if (!atomic_file::write(source / L"hana" / L"renamed.bmp", first) || !atomic_file::write(source / L"hana" / L"renamed.txt", "different description")) return 11;
    if (f::pick(source, "hana", *state, 3) || f::publish(folder, *draft, L"Duplicate", day + 1, now + 86400)) return 12;
    // The second publication is allowed; the third is refused even across reloading and repeated opens.
    f::Draft d2{f::fingerprint(second), second, L"A second day-to-day scene"};
    f::Draft d3{f::fingerprint(third), third, L"A third scene"};
    state = f::publish(folder, d2, L"Second post", day, now + f::publication_gap);
    if (!state || state->posts.size() != 2 || f::due(*state, day, now + 9 * 3600) || f::publish(folder, d3, L"Too many", day, now + 9 * 3600)) return 13;
    const auto tomorrow = f::publish(folder, d3, L"Tomorrow", day + 1, now + 86400);
    if (!tomorrow || tomorrow->posts.size() != 3) return 14;
    // Permanent records remain authoritative when a source or cached image goes missing.
    std::error_code ec;
    std::filesystem::remove(source / L"hana" / L"1.bmp", ec);
    std::filesystem::remove(folder / L"media" / (text_encoding::wideLenient(draft->id) + L".img"), ec);
    state = f::load(folder);
    if (!state || state->posts.size() != 3 || f::pick(source, "hana", *state, 9)) return 15;
    // Other characters have independent archives and quotas.
    if (!f::load(f::folderOf(root, "rin"))->posts.empty()) return 16;
    const auto context = f::context(*state, L"magazyn", false);
    if (context.find(L"Sesja tylko dla mnie") == std::wstring::npos || context.find(L"nie zakładaj") == std::wstring::npos) return 17;
    if (f::generationRequest(draft->description, false).find(draft->description) == std::wstring::npos ||
        f::generationPrompt(companion::Persona::genki, companion_fixture::hana, false, L"Public goal").find(L"Public goal") == std::wstring::npos) return 18;
    // Hearts persist, can be undone, and toggling never rerolls the one-time reaction decision.
    auto social = f::setLike(folder, draft->id, true, 0);
    if (!social || !social->liked || !social->reaction_pending || !social->reaction_private) return 30;
    social = f::setLike(folder, draft->id, false, 0);
    if (!social || social->liked || social->reaction_pending) return 31;
    social = f::setLike(folder, draft->id, true, 0);
    if (!social || social->reaction_pending || !f::loadSocial(folder, draft->id)->liked) return 32;
    // Not every comment gets a reply. Empty/oversize input never changes the durable thread.
    social = f::addComment(folder, draft->id, L"Świetne zdjęcie!", now + 200, 99);
    if (!social || social->reaction_pending || social->comments.size() != 1 || !social->comments[0].player ||
        f::addComment(folder, draft->id, L"  ", now, 0) || f::addComment(folder, draft->id, std::wstring(301, L'x'), now, 0)) return 33;
    social = f::addComment(folder, d2.id, L"Gdzie to było?", now + 300, 0);
    if (!social || !social->reaction_pending || social->reaction_private) return 34;
    if (f::completeReaction(folder, d2.id, L"wrong event", L"Odpowiedź", L"Hana", "hana", now)) return 35;
    social = f::completeReaction(folder, d2.id, social->reaction_context, L"Na spacerze!", L"Hana", "hana", now + 400);
    if (!social || social->reaction_pending || !social->reaction_done || social->comments.size() != 2 || social->comments[1].look != "hana") return 36;
    // Three distinct authored profiles, strict JSON, stable seeding and no overwritten player comments.
    std::vector<f::CommentAuthor> authors{{L"mila.jpg", L"warm", {}}, {L"neonfox", L"playful", {}}, {L"Rin", L"friendly", "rin"}};
    const auto generated = f::parseComments(L"```json\n[\"Piękne światło.\",\"Świetny kadr!\",\"Ten kolor do ciebie pasuje.\"]\n```", authors, now + 500);
    if (generated.size() != 3 || generated[2].look != "rin" || !f::parseComments(L"[\"only one\"]", authors, now).empty()) return 37;
    social = f::seedComments(folder, d2.id, generated);
    if (!social || !social->seeded || social->comments.size() != 5 || f::seedComments(folder, d2.id, generated)) return 38;
    // A like sometimes leads to one private message, claimed at most once across reloads.
    social = f::setLike(folder, d3.id, true, 1);
    if (!social || !social->reaction_pending) return 39;
    social = f::completeReaction(folder, d3.id, social->reaction_context, L"Widzę twoje serduszko, dzięki!", L"Hana", "hana", now + 600);
    if (!social || social->private_text.empty() || !social->comments.empty()) return 40;
    social = f::claimPrivate(folder, d3.id);
    if (!social || !social->private_claimed || f::claimPrivate(folder, d3.id)) return 41;
    const auto reloaded = f::load(folder);
    if (!reloaded || reloaded->posts[0].social.private_text.empty() || reloaded->posts[0].width != 1 || reloaded->posts[0].height != 1) return 42;
    if (f::context(*reloaded, L"magazyn", false).find(L"Świetne zdjęcie!") == std::wstring::npos) return 43;
    // A malformed record must never look like an unused photo pool.
    if (!atomic_file::write(folder / L"posts" / L"damaged.json", "{")) return 19;
    if (f::load(folder) || f::publish(folder, d3, L"No", day + 2, now + 2 * 86400)) return 20;
    // Scroll/hit geometry: a clipped off-screen button can't receive a click.
    companion_ui::Feed view; view.archive.posts.resize(3); view.app_age = 2.1f; view.entrance.snap(1);
    const companion_ui::FeedLayout layout{1600, 900};
    const auto button = companion_ui::FeedLayout::action(view.archive, 1, 0, 0);
    const auto hit = [&](float x, float y) { return layout.hit(layout.left() + x * layout.scale(), layout.upper() + y * layout.scale(), view); };
    if (hit(button.left + 5, button.top + 5) != companion_ui::FeedLayout::no_hit) return 21;
    view.motion.snap(150);
    const auto scrolled = companion_ui::FeedLayout::action(view.archive, 0, 0, 150);
    if (hit(scrolled.left + 5, scrolled.top + 5) != 0 || companion_ui::FeedLayout::limit({}) != 0) return 22;
    view.app_age = 1.9f;
    if (hit(scrolled.left + 5, scrolled.top + 5) != companion_ui::FeedLayout::no_hit) return 44;
    view.app_age = 2.1f; view.exiting = true;
    if (view.ready()) return 45;
    f::Post portrait; portrait.width = 768; portrait.height = 1280;
    if (std::abs(companion_ui::FeedLayout::photoHeight(portrait) - 653.3333f) > 0.01f) return 46;
    return 0;
}
}
int main() {
    const int result = run();
    if (result) std::cerr << "Launchfeed case failed: " << result << '\n';
    return result;
}
