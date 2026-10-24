// Renderer: the right drawer's content (a plugin page, the dashboard or Steam friends) and the
// Steam friends list.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "friends_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

void Renderer::drawFriends() {
    const float progress = smoothFade(context_->friends_progress);
    if (progress < 0.004f) return;
    const float width = friends_layout::drawer_width, height = viewport_.height;
    const float x = viewport_.width - width * progress;
    // The drawer has 90% black beneath it; a smooth fade reaches the clear
    // scene one-third of a viewport away from its edge. With glass the scene under the drawer
    // is frosted and the black lets more of it through.
    if (desktop_style_) {
        // The desktop window: a solid side panel, as its other panels, over the barely dimmed window.
        brush(D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.12f * progress));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, height), solid_.Get());
        brush(D2D1::ColorF(0.082f, 0.088f, 0.102f));
        target_->FillRectangle(D2D1::RectF(x, 0, viewport_.width, height), solid_.Get());
        brush(D2D1::ColorF(1, 1, 1, 0.08f));
        target_->FillRectangle(D2D1::RectF(x, 0, x + 1, height), solid_.Get());
    } else if (const bool frosted = frostPane(D2D1::RectF(x, 0, viewport_.width, height), progress); friends_backdrop_) {
        const float edge = viewport_.width - width;
        friends_backdrop_->SetStartPoint(D2D1::Point2F(edge - viewport_.width * drawer_style_.fade_fraction, 0));
        friends_backdrop_->SetEndPoint(D2D1::Point2F(edge, 0));
        friends_backdrop_->SetOpacity(progress * drawer_style_.opacity * (frosted ? 1.0f - 0.3f * glass_level_ : 1.0f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, height), friends_backdrop_.Get());
    }
    if (context_->plugin_drawer) drawPluginPage(x, true);
    else if (context_->dashboard.list) drawFriendsList(x);
    else drawDashboard(x);
}

// Steam friends list inside the drawer, opened from the dashboard's Steam friends tile.
void Renderer::drawFriendsList(float x) {
    const float width = friends_layout::drawer_width, height = viewport_.height;
    // Back button: same shape as the grid screens' one; B/Esc do the same.
    const auto back = friends_layout::listBack(x);
    // Focus: one frame gliding between Back and the rows (ViewContext::drawer_glide, drawer-relative x).
    const auto& glide = context_->drawer_glide;
    const auto highlight = [&](float alpha) {
        if (!glide) return;
        brush(color(0.88f, 0.88f, 0.88f, alpha));
        const auto r = D2D1::RectF(x + glide->left, glide->top, x + glide->right, glide->bottom);
        target_->FillRoundedRectangle(r.bottom - r.top <= 64 ? pill(r) : D2D1::RoundedRect(r, 16, 16), solid_.Get());
    };
    highlight(0.13f);
    // Back: a glass circle with the dot; the focus frame gliding onto it marks it as well.
    const bool back_focus = context_->friends_back || (context_->friends && context_->friends->friends.empty());
    brush(drawButton(D2D1::RectF(back.left, back.top, back.right, back.bottom), back_focus ? 1.0f : 0.0f, 1, false));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f), 3, 3), solid_.Get());
    textLine(tr(L"ZNAJOMI"), back.right + 14, (back.top + back.bottom) * 0.5f, width - (back.right - x) - 40, category_font_.Get(), color(0.93f, 0.93f, 0.93f));
    if (context_->friends_loading) {
        text(tr(L"Pobieranie ze Steam…"), x + 26, 121, width - 50, 34,
             toast_sub_font_.Get(), color(0.71f, 0.71f, 0.71f));
        return;
    }
    if (!context_->friends || !context_->friends->error.empty()) {
        const auto value = context_->friends ? context_->friends->error : std::wstring(tr(L"Brak danych Steam"));
        text(value, x + 26, 122, width - 50, 78, toast_sub_font_.Get(),
             color(0.78f, 0.78f, 0.78f));
        return;
    }
    const auto& people = context_->friends->friends;
    if (people.empty()) {
        text(tr(L"Lista znajomych jest pusta"), x + 26, 122, width - 50, 36,
             toast_sub_font_.Get(), color(0.71f, 0.71f, 0.71f));
        return;
    }
    target_->PushAxisAlignedClip(D2D1::RectF(x + 1, 104, viewport_.width, height - 12),
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float y = 115.0f - context_->friends_scroll_glide.value_or(static_cast<float>(context_->friends_scroll));
    constexpr const wchar_t* headings[] = {trKey(L"W GRZE"), trKey(L"DOSTĘPNI"), trKey(L"NIEDOSTĘPNI")};
    for (int group = 0; group < 3; ++group) {
        const auto matches = [group](const SteamFriend& person) {
            return group == 0 ? !person.game.empty() : group == 1 ? person.game.empty() && person.status > 0 :
                   person.game.empty() && person.status == 0;
        };
        const size_t count = static_cast<size_t>(std::count_if(people.begin(), people.end(), matches));
        if (!count) continue;
        if (y >= 95 && y <= height)
            text(std::wstring(tr(headings[group])) + L"  " + std::to_wstring(count), x + 30, y + 5, width - 55, 33,
                 friend_section_font_.Get(), color(0.78f, 0.78f, 0.78f));
        y += friends_layout::section_height;
        for (size_t index = 0; index < people.size(); ++index) {
            const auto& person = people[index];
            if (!matches(person)) continue;
            if (y + friends_layout::row_height >= 104 && y < height - 8) {
                if (!glide && !context_->friends_back && index == context_->friends_cursor) {
                    brush(color(0.88f, 0.88f, 0.88f, 0.13f));
                    const auto row = D2D1::RectF(x + 18, y + 5, x + width - 17, y + friends_layout::row_height - 5);
                    target_->FillRoundedRectangle(row.bottom - row.top <= 64 ? pill(row) : D2D1::RoundedRect(row, 16, 16), solid_.Get());
                }
                const float ax = x + 30, ay = y + 14;
                const auto found = avatars_.find(person.id);
                if (found != avatars_.end() && found->second.bitmap) {
                    target_->DrawBitmap(found->second.bitmap.Get(), D2D1::RectF(ax, ay, ax + 52, ay + 52));
                } else {
                    brush(color(0.133f, 0.133f, 0.133f)); // #222
                    target_->FillRoundedRectangle(D2D1::RoundedRect(
                        D2D1::RectF(ax, ay, ax + 52, ay + 52), 9, 9), solid_.Get());
                }
                // Name and status as one group on the avatar's centre line.
                const float shade = group == 0 ? 0.79f : 0.62f;
                textGroup(person.name, person.game.empty() ? std::wstring(person.status ? tr(L"Dostępny") : tr(L"Niedostępny")) : person.game,
                          x + 101, ay + 26, width - 122, friend_name_font_.Get(), color(0.95f, 0.95f, 0.95f),
                          friend_status_font_.Get(), color(shade, shade, shade), 1);
            }
            y += friends_layout::row_height;
        }
    }
    target_->PopAxisAlignedClip();
}
