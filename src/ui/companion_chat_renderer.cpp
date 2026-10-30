// Her chat screen (src/ui/companion_view.hpp): the stage with her (and a guest), the header, the
// log of messages with links, photos and the roulette, the input with chips, the menu, the photo
// viewer, the wardrobe, and the reset and question dialogs.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "../net/companion_links.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
using render_detail::box;
using render_detail::smoothFade;
}

void Renderer::drawCompanionWardrobe(const companion_ui::Layout& layout, const companion_ui::Chat& chat) {
    const auto* view = context_->companion;
    if (!view) return;
    using W = companion_ui::Layout;
    const auto panel = box(layout.menu(chat));
    glassFill(glassPanel(D2D1::RoundedRect(panel, 22, 22)), D2D1::ColorF(0.022f, 0.024f, 0.027f, 0.93f));
    if (panel_borders_) { brush(gray(0.9f, 0.63f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(panel, 22, 22), solid_.Get(), 1.4f); }
    textLine(tr(L"Zmień strój"), panel.left + W::wardrobe_padding + 8, panel.top + W::wardrobe_padding + W::wardrobe_title * 0.45f,
             panel.right - panel.left - 2 * W::wardrobe_padding, menu_font_.Get(), gray(0.96f));
    avatar::Pose still; // Her picture in each outfit: calm, smiling, unmoved.
    still.emotion = still.previous = companion::Emotion::smile;
    for (size_t i = 0; i < chat.menu_items.size(); ++i) {
        const auto& item = chat.menu_items[i];
        const auto tile = box(layout.menuItem(chat, static_cast<int>(i)));
        const bool focused = chat.menu_focus == static_cast<int>(i), hovered = chat.hover == companion_ui::menuFocus(static_cast<int>(i));
        const bool worn = item.detail == L"\u2713";
        const auto shape = D2D1::RoundedRect(tile, 18, 18);
        brush(gray(1, focused ? 0.16f : hovered ? 0.11f : 0.06f)); target_->FillRoundedRectangle(shape, solid_.Get());
        // Her picture in it, clipped to the tile's upper part; a locked one is only a faint hint.
        const auto thumb = D2D1::RectF(tile.left, tile.top, tile.right, tile.top + W::wardrobe_thumb);
        target_->PushAxisAlignedClip(D2D1::RectF(thumb.left + 1, thumb.top + 1, thumb.right - 1, thumb.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float height = W::wardrobe_thumb * 1.12f;
        drawCompanionFigure(view->look, still, (thumb.left + thumb.right) * 0.5f, thumb.bottom + height * 0.06f, height, item.enabled ? 1.0f : 0.3f, item.value);
        target_->PopAxisAlignedClip();
        brush(gray(1, 0.1f)); target_->DrawLine(D2D1::Point2F(tile.left + 12, thumb.bottom), D2D1::Point2F(tile.right - 12, thumb.bottom), solid_.Get(), 1);
        // Its name, and below it "od poziomu 10" (locked) or "Założony" (worn).
        const float caption_top = thumb.bottom + 6;
        textLine(item.label, tile.left + 12, caption_top + 15, tile.right - tile.left - 24, settings_body_font_.Get(),
                 item.enabled ? gray(0.97f) : gray(0.55f, 0.85f), DWRITE_TEXT_ALIGNMENT_CENTER);
        const std::wstring detail = worn ? std::wstring(tr(L"Założony")) : item.detail;
        if (!detail.empty())
            textLine(detail, tile.left + 12, caption_top + 37, tile.right - tile.left - 24, friend_status_font_.Get(),
                     item.enabled ? uiAccent(0.95f) : gray(0.55f, 0.85f), DWRITE_TEXT_ALIGNMENT_CENTER);
        // The worn one keeps an accent rim; focus is a bright white one.
        if (worn) { brush(uiAccent(0.75f)); target_->DrawRoundedRectangle(shape, solid_.Get(), 1.6f); }
        if (focused) { brush(gray(1, 0.92f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tile.left - 2, tile.top - 2, tile.right + 2, tile.bottom + 2), 20, 20), solid_.Get(), 2.4f); }
        else if (hovered) { brush(gray(1, 0.4f)); target_->DrawRoundedRectangle(shape, solid_.Get(), 1.2f); }
    }
}

void Renderer::drawCompanion() {
    drawBackground();
    if (context_->desktop_window) { // The desktop window: almost black, the chosen game only glimmering through.
        brush(D2D1::ColorF(0.012f, 0.013f, 0.016f, 0.9f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    } else drawFrostedBackdrop(0.70f);
    const auto* view = context_->companion;
    if (!view || !view->chat) return;
    beginScreenForeground();
    const auto& chat = *view->chat;
    if (chat.feed) { companion_links_.clear(); companion_videos_.clear(); companion_photo_rect_ = {}; drawCompanionFeed(); return; }
    companion_feed_images_.clear();
    const companion_ui::Layout layout{viewport_.width, viewport_.height};
    const float enter = smoothFade(chat.age / 0.45f);
    // Each companion's colour: her light behind her, her name, her bubbles' rim.
    const auto tint = [](companion::Look look, float alpha) { const auto t = companion_ui::tint(look); return D2D1::ColorF(t.r, t.g, t.b, alpha); };
    const auto light = [](companion::Look look, float alpha) { const auto t = companion_ui::tint(look); return D2D1::ColorF(0.5f + 0.5f * t.r, 0.5f + 0.5f * t.g, 0.5f + 0.5f * t.b, alpha); };
    const auto spotlight = [&](companion::Look look, const companion_ui::Figure& at, float alpha) {
        if (alpha <= 0.003f) return;
        const auto t = companion_ui::tint(look); // Her colour, a little deeper.
        D2D1_GRADIENT_STOP stops[2]{{0, D2D1::ColorF(t.r * 0.9f, t.g * 0.85f, t.b * 0.92f, 0.2f * alpha)}, {1, D2D1::ColorF(0, 0, 0, 0)}};
        const float radius = at.height / 1.8f;
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1RadialGradientBrush> glow;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 2, collection.GetAddressOf())) &&
            SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(at.x, at.bottom - at.height * 0.6f), {}, radius, radius * 1.19f),
                                                        collection.Get(), glow.GetAddressOf())))
            target_->FillRectangle(D2D1::RectF(at.x - radius * 1.07f, 0, at.x + radius * 1.07f, viewport_.height), glow.Get());
    };
    // A guest stands behind her, smaller, and she makes room; both fit where she stood alone.
    const float duo = view->guest ? smoothFade(view->duo) : 0.0f;
    const auto pair = layout.pair(duo);
    if (view->guest && view->duo > 0.003f) {
        auto guest = layout.pair(1).guest;
        const float t = std::clamp(view->duo, 0.0f, 1.0f);
        float alpha = std::min(1.0f, t * 2.0f);
        if (view->guest_leaving) guest.x -= (1 - t) * guest.height * 0.9f; // She runs off to the side.
        else guest.bottom += (1 - smoothFade(t)) * guest.height * 0.45f;       // She pops up from below.
        alpha *= enter;
        spotlight(*view->guest, guest, alpha);
        drawCompanionFigure(*view->guest, view->guest_pose, guest.x, guest.bottom, guest.height, alpha, view->guest_costume);
    }
    // Her entrance: a full-figure pose in her outfit fills the left column for a few seconds (when
    // the outfit has one), then her close-up takes its place.
    float host_in = enter;
    if (chat.pose_age < companion_ui::pose_hold + companion_ui::pose_out) {
        const auto area = layout.poseArea();
        const float shown = companion_ui::poseAlpha(chat.pose_age);
        const float slide = (1 - smoothFade(chat.pose_age / companion_ui::pose_in)) * 60 - smoothFade((chat.pose_age - companion_ui::pose_hold) / companion_ui::pose_out) * 30;
        const companion_ui::Figure column{(area.left + area.right) * 0.5f, area.bottom, (area.bottom - area.top) * 0.9f};
        spotlight(view->look, column, shown);
        if (drawCompanionPose(view->look, view->costume, area, shown, slide)) host_in = std::min(enter, companion_ui::afterPose(chat.pose_age));
    }
    spotlight(view->look, pair.host, host_in);
    drawCompanionFigure(view->look, view->pose, pair.host.x, pair.host.bottom + (1 - host_in) * 40, pair.host.height, host_in, view->costume);
    // Back, their names, the connection and "…".
    const auto back = layout.back();
    const float back_focus = chat.focus == companion_ui::focus_back && !chat.menu ? 1.0f : 0.0f;
    drawButtonGlass(box(back), 1, false, chat.hover == companion_ui::focus_back);
    drawButtonFocus(box(back), back_focus);
    const auto ink = buttonInk(back_focus);
    const float cy = (back.top + back.bottom) * 0.5f;
    line(back.left + 22, cy, back.left + 34, cy, ink, 2);
    line(back.left + 22, cy, back.left + 28, cy - 6, ink, 2);
    line(back.left + 22, cy, back.left + 28, cy + 6, ink, 2);
    float name_x = back.right + 26;
    textLine(view->name, name_x, cy, 360, settings_heading_font_.Get(), light(view->look, 1));
    if (view->guest && duo > 0.003f) {
        name_x += textAdvance(view->name, settings_heading_font_.Get());
        textLine(L" & ", name_x, cy, 80, settings_heading_font_.Get(), gray(0.6f, duo));
        name_x += textAdvance(L" & ", settings_heading_font_.Get());
        textLine(view->guest_name, name_x, cy, 300, settings_heading_font_.Get(), light(*view->guest, duo));
        name_x += textAdvance(view->guest_name, settings_heading_font_.Get()) * duo;
    } else name_x += textAdvance(view->name, settings_heading_font_.Get());
    // Gamification: her level and sympathy right after the name(s).
    if (view->affinity) drawCompanionAffinity(name_x + 16, cy, enter);
    const auto more = layout.more();
    const auto personas = view->guest && duo > 0.5f ? view->persona + L" & " + view->guest_persona : view->persona;
    textLine(view->status.empty() ? personas : personas + L" · " + view->status, layout.left(), cy, (chat.archive ? more.right : layout.feed().left - 18) - layout.left(),
             friend_status_font_.Get(), view->chat->offline && !chat.archive ? palette::danger(0.9f) : palette::muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (!chat.archive) { // A history page has no menu.
        const auto phone = layout.feed();
        const float focus = chat.focus == companion_ui::focus_feed ? 1.0f : 0.0f;
        drawButtonGlass(box(phone), 1, false, chat.hover == companion_ui::focus_feed);
        drawButtonFocus(box(phone), focus);
        brush(buttonInk(focus));
        const float x = (phone.left + phone.right) * 0.5f;
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 10, cy - 17, x + 10, cy + 17), 5, 5), solid_.Get(), 1.8f);
        target_->DrawLine(D2D1::Point2F(x - 3, cy - 12), D2D1::Point2F(x + 3, cy - 12), solid_.Get(), 2);
        target_->DrawLine(D2D1::Point2F(x - 4, cy + 12), D2D1::Point2F(x + 4, cy + 12), solid_.Get(), 1.5f);
        const float more_focus = chat.focus == companion_ui::focus_more || chat.menu ? 1.0f : 0.0f;
        drawButtonGlass(box(more), 1, false, chat.hover == companion_ui::focus_more);
        drawButtonFocus(box(more), more_focus);
        for (int i = -1; i <= 1; ++i) {
            brush(buttonInk(more_focus));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((more.left + more.right) * 0.5f + i * 8.0f, cy), 2.6f, 2.6f), solid_.Get());
        }
    }
    // The conversation, newest at the bottom; her last line is typed out as she speaks. Each
    // companion's run of messages is signed with her name in her colour.
    auto* font = settings_body_font_.Get();
    const float top = layout.logTop(), bottom = layout.logBottom(chat);
    const float column = layout.right() - layout.left();
    const float max_width = std::min(720.0f, column * 0.78f);
    const float scroll = chat.scroll_motion.value;
    target_->PushAxisAlignedClip(D2D1::RectF(layout.left() - 30, top, layout.right() + 4, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float y = bottom + scroll;
    float content = 0;
    constexpr float name_height = 24;
    const auto signature = [&](companion::Look speaker, float left) {
        y -= name_height; content += name_height;
        if (y + name_height >= top - 4 && y <= bottom + 4)
            textLine(companion::name(speaker), left + 6, y + name_height * 0.5f - 2, 300, friend_section_font_.Get(), tint(speaker, 0.95f));
    };
    const auto bubble = [&](const std::wstring& value, bool user, bool typing, bool error, companion::Look speaker, bool named,
                            const std::vector<companion::GameMention>& mentions) {
        const float padding_x = 18, padding_y = 12;
        const auto text = companionText(value, mentions, max_width - 2 * padding_x, 40);
        const float text_width = typing ? 54 : text.width;
        const float text_height = typing ? typeMetrics(font).line : text.height;
        const float w = text_width + 2 * padding_x, h = text_height + 2 * padding_y;
        y -= h; content += h + 14;
        const float left = user ? layout.right() - w : layout.left();
        const auto rect = D2D1::RectF(left, y, left + w, y + h);
        if (rect.bottom >= top - 4 && rect.top <= bottom + 4) {
            const auto shape = D2D1::RoundedRect(rect, 18, 18);
            if (user) brush(gray(0.96f, 0.95f));
            else if (error) brush(D2D1::ColorF(0.32f, 0.12f, 0.14f, 0.78f));
            else brush(D2D1::ColorF(0.13f, 0.14f, 0.18f, 0.82f));
            target_->FillRoundedRectangle(shape, solid_.Get());
            if (!user) { brush(tint(speaker, 0.34f)); target_->DrawRoundedRectangle(shape, solid_.Get(), 1.2f); }
            if (typing) {
                for (int i = 0; i < 3; ++i) {
                    const float lift = std::max(0.0f, std::sin(elapsed_ * 7 - i * 0.9f)) * 4;
                    brush(gray(0.85f));
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + padding_x + 9 + i * 18.0f, (rect.top + rect.bottom) * 0.5f - lift), 4.2f, 4.2f), solid_.Get());
                }
            } else drawCompanionText(text, value, mentions, rect.left + padding_x, rect.top + padding_y, user ? gray(0.08f) : gray(0.96f),
                                     D2D1::RectF(layout.left() - 30, top, layout.right() + 4, bottom), speaker);
        }
        if (named && !user) signature(speaker, left);
        y -= 14;
    };
    // A message opens a run of hers (and is signed) after the player, a card or someone else.
    const auto opens = [&](size_t i) {
        const auto& message = chat.log[i];
        return !message.user && (i == 0 || chat.log[i - 1].user || chat.log[i - 1].roulette || chat.log[i - 1].speaker != message.speaker);
    };
    const bool spinning = !chat.log.empty() && chat.log.back().roulette && !chat.log.back().roulette->landed();
    // Three dots while a reply is on its way (not while its streamed words appear), or while the
    // next of its messages is being typed. When `hesitating`, the dots briefly vanish (0.9..1.55 s)
    // as if she typed an impulsive reply, deleted it, and started typing again.
    const bool typing_next = !chat.queue.empty() && !chat.revealing();
    const bool hesitation_pause = chat.waiting && chat.hesitating && chat.hesitation_clock >= 0.9f && chat.hesitation_clock < 1.55f;
    if (((chat.waiting && !hesitation_pause) && !spinning && !chat.revealing()) || typing_next) {
        const auto speaker = typing_next ? chat.queue.front().speaker : view->look;
        const bool named = chat.log.empty() || chat.log.back().user || chat.log.back().roulette || chat.log.back().speaker != speaker;
        bubble({}, false, true, false, speaker, named, {});
    }
    // Her picture: decoded once per chat, drawn in its own proportions.
    if (companion_photo_.pixels != view->photo) {
        companion_photo_ = {}; companion_photo_.pixels = view->photo; companion_photo_.short_edge = 640;
        if (view->photo) createBitmap(companion_photo_);
    } else if (view->photo && !companion_photo_.bitmap) createBitmap(companion_photo_);
    companion_photo_rect_ = {}; // Where a click enlarges it (its visible part).
    const std::vector<companion::GameMention> no_mentions;
    for (size_t i = chat.log.size(); i-- > 0;) {
        const auto& message = chat.log[i];
        if (message.user) {
            // Cards are derived from the saved message, so old conversations keep working too.
            const auto links = companion::links::find(message.text);
            for (auto it = links.rbegin(); it != links.rend(); ++it) {
                if (it->video_id.empty()) continue;
                constexpr float h = 92;
                const float w = std::min(max_width, 390.0f), left = layout.right() - w;
                y -= h; content += h + 14;
                const auto rect = D2D1::RectF(left, y, left + w, y + h);
                if (rect.bottom > top && rect.top < bottom) {
                    const settings_ui::Rect visible{rect.left, std::max(rect.top, top), rect.right, std::min(rect.bottom, bottom)};
                    const bool hovered = view->link_pointer && visible.contains(view->link_pointer->first, view->link_pointer->second);
                    brush(D2D1::ColorF(0.15f, 0.08f, 0.10f, 0.96f)); target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 16, 16), solid_.Get());
                    brush(D2D1::ColorF(0.95f, 0.25f, 0.3f, hovered ? 0.95f : 0.45f));
                    target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, 16, 16), solid_.Get(), hovered ? 2.0f : 1.0f);
                    const float cx = left + 36, center_y = y + h * 0.5f;
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, center_y), 20, 20), solid_.Get());
                    textLine(L"▶", cx - 12, center_y, 24, settings_body_font_.Get(), gray(1), DWRITE_TEXT_ALIGNMENT_CENTER);
                    textLine(L"YouTube · " + std::wstring(tr(L"Odtwórz film")), left + 70, y + 32, w - 88, settings_body_font_.Get(), gray(0.97f));
                    textLine(text_encoding::wideLenient(it->video_id), left + 70, y + 61, w - 88, friend_status_font_.Get(), gray(0.66f));
                    companion_videos_.push_back({visible, it->url});
                }
                y -= 14;
            }
        }
        if (message.photo) {
            float w = std::min(companion_ui::photo_width, max_width), h = w * 1.33f;
            if (companion_photo_.bitmap) {
                const auto size = companion_photo_.bitmap->GetSize();
                if (size.width > 0) h = std::min(companion_ui::photo_max_height, w * size.height / size.width);
                if (size.height > 0 && h >= companion_ui::photo_max_height) w = h * size.width / size.height;
            }
            y -= h; content += h + 14;
            const auto rect = D2D1::RectF(layout.left(), y, layout.left() + w, y + h);
            if (rect.bottom >= top - 4 && rect.top <= bottom + 4) {
                const auto shape = D2D1::RoundedRect(rect, 18, 18);
                brush(D2D1::ColorF(0.13f, 0.14f, 0.18f, 0.82f)); target_->FillRoundedRectangle(shape, solid_.Get());
                if (companion_photo_.bitmap) fillImage(companion_photo_, rect, 18, 1);
                const bool hovered = view->photo && chat.hover == companion_ui::photo_hit && !chat.viewer;
                brush(hovered ? tint(message.speaker, 0.8f) : tint(message.speaker, 0.34f)); target_->DrawRoundedRectangle(shape, solid_.Get(), hovered ? 2.0f : 1.2f);
                if (view->photo) companion_photo_rect_ = {rect.left, std::max(rect.top, top), rect.right, std::min(rect.bottom, bottom)};
            }
            if (opens(i)) signature(message.speaker, layout.left());
            y -= 14;
            continue;
        }
        if (message.roulette) {
            const float h = drawCompanionRoulette(*message.roulette, layout.left(), y, std::min(max_width, 460.0f), 1);
            y -= h + 14; content += h + 14;
            continue;
        }
        const bool last = i + 1 == chat.log.size();
        const auto shown = last && !message.user ? message.text.substr(0, std::min(chat.reveal, message.text.size())) : message.text;
        if (shown.empty() && !message.user) continue;
        bubble(shown, message.user, false, message.error, message.speaker, opens(i), message.user ? no_mentions : message.mentions);
    }
    target_->PopAxisAlignedClip();
    companion_scroll_limit_ = std::max(0.0f, content - (bottom - top));
    // Suggestions, the field and Send.
    const auto chips = layout.chips(chat);
    for (size_t i = 0; i < chips.size(); ++i) {
        const auto chip = box(chips[i]);
        const float focus = chat.focus == static_cast<int>(i) ? 1.0f : 0.0f;
        const auto label_ink = drawButton(chip, focus, enter, false, chat.hover == static_cast<int>(i));
        textLine(chat.chips[i].label, chip.left + 18, (chip.top + chip.bottom) * 0.5f, chip.right - chip.left - 36, settings_body_font_.Get(), label_ink,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    const auto field = box(layout.input(chat));
    if (chat.archive) {
        // A history page: no field, only what it is.
        textLine(view->chat_note, field.left, (field.top + field.bottom) * 0.5f, layout.right() - field.left, font, gray(0.55f), DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
    const bool typing = chat.focus == companion_ui::focus_input && !chat.ended;
    drawButtonGlass(field, chat.ended ? 0.55f : 1.0f, false, chat.hover == companion_ui::focus_input && !chat.ended);
    if (typing) { brush(uiAccent(0.9f)); target_->DrawRoundedRectangle(desktop_style_ ? D2D1::RoundedRect(field, 8, 8) : pill(field), solid_.Get(), 1.6f); }
    const float line_height = typeMetrics(font).line;
    if (chat.ended) // The conversation is over: why, and how long she rests.
        textLine(view->chat_note, field.left + 24, (field.top + field.bottom) * 0.5f, field.right - field.left - 48, font, gray(0.62f));
    else if (chat.input.empty()) {
        const auto to = view->guest && !view->guest_leaving ? view->name + tr(L" i ") + view->guest_name : view->name;
        textLine(std::wstring(tr(L"Napisz do ")) + to + L"…", field.left + 24, (field.top + field.bottom) * 0.5f, field.right - field.left - 48, font, gray(0.6f));
        if (typing && std::fmod(elapsed_, 1.0f) < 0.6f) fieldText(L"", 0, true, field.left + 22, (field.top + field.bottom - line_height) * 0.5f, 20, line_height, font, gray(0.95f));
    } else {
        // The end of a long message stays visible.
        std::wstring shown = chat.input;
        const float room = field.right - field.left - 48;
        while (shown.size() > 1 && textAdvance(shown, font) > room) shown.erase(0, 1);
        fieldText(shown, shown.size(), typing && std::fmod(elapsed_, 1.0f) < 0.6f, field.left + 24, (field.top + field.bottom - line_height) * 0.5f,
                  room, line_height, font, gray(0.97f));
    }
    const auto send = box(layout.send(chat));
    const bool can_send = chat.ended ? view->can_restart : !chat.input.empty() && !chat.waiting;
    const auto send_ink = drawButton(send, chat.focus == companion_ui::focus_send ? 1.0f : 0.0f, 1, false, chat.hover == companion_ui::focus_send, can_send);
    textLine(chat.ended ? tr(L"Nowa rozmowa") : tr(L"Wyślij"), send.left, (send.top + send.bottom) * 0.5f, send.right - send.left, settings_body_font_.Get(), send_ink,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // Diagnostics ("…" → Pokaż diagnostykę): where the last answer's time went, in the free space
    // above the figures.
    if (!view->diagnostics.empty()) {
        constexpr float row = 22, padding = 18;
        const float width = std::min(640.0f, layout.left() - 56 - 24);
        const auto panel = D2D1::RectF(56, 116, 56 + width, 116 + 2 * padding + 8 + row * static_cast<float>(view->diagnostics.size()));
        glassFill(glassPanel(D2D1::RoundedRect(panel, 18, 18)), D2D1::ColorF(0.022f, 0.024f, 0.027f, 0.86f));
        brush(gray(1, 0.12f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(panel, 18, 18), solid_.Get(), 1);
        float line_y = panel.top + padding + row * 0.5f;
        for (size_t i = 0; i < view->diagnostics.size(); ++i) {
            const bool heading = i == 0, lead = i == 1 || i == 2;
            textLine(view->diagnostics[i], panel.left + padding, line_y, width - 2 * padding, heading ? friend_section_font_.Get() : friend_status_font_.Get(),
                     heading ? uiAccent(0.95f) : lead ? gray(0.96f) : gray(0.72f));
            line_y += row + (heading ? 8 : 0);
        }
    }
    // "…": call another companion in or say goodbye to her, and clear the chat.
    if (chat.menu && companion_ui::Layout::wardrobe(chat)) drawCompanionWardrobe(layout, chat);
    else if (chat.menu && !chat.menu_items.empty()) {
        const auto panel = box(layout.menu(chat));
        glassFill(glassPanel(D2D1::RoundedRect(panel, 22, 22)), D2D1::ColorF(0.022f, 0.024f, 0.027f, 0.93f));
        if (panel_borders_) { brush(gray(0.9f, 0.63f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(panel, 22, 22), solid_.Get(), 1.4f); }
        for (size_t i = 0; i < chat.menu_items.size(); ++i) {
            const auto& item = chat.menu_items[i];
            const auto row = box(layout.menuRow(static_cast<int>(i)));
            const bool focused = chat.menu_focus == static_cast<int>(i), hovered = chat.hover == companion_ui::menuFocus(static_cast<int>(i));
            if (focused || hovered) { brush(gray(0.86f, focused ? 0.16f : 0.08f)); target_->FillRoundedRectangle(pill(row), solid_.Get()); }
            const bool clear = item.kind == companion_ui::MenuItem::Kind::clear && !chat.ended; // Ending is red; a new one is not.
            const auto label_ink = !item.enabled ? gray(0.5f, 0.75f) : clear ? palette::danger(1) : gray(0.96f);
            const float detail_w = item.detail.empty() ? 0.0f : textAdvance(item.detail, friend_status_font_.Get()) + 12;
            textLine(item.label, row.left + 22, (row.top + row.bottom) * 0.5f, row.right - row.left - 44 - detail_w, menu_font_.Get(), label_ink);
            if (!item.detail.empty()) // "od poziomu 10" for a locked costume, ✓ for the one she wears.
                textLine(item.detail, row.left + 22, (row.top + row.bottom) * 0.5f, row.right - row.left - 44, friend_status_font_.Get(),
                         item.enabled ? uiAccent(0.95f) : gray(0.55f, 0.85f), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }
    if (chat.confirm) drawCompanionResetDialog(layout, chat);
    // Her photo enlarged over everything: a dim, the picture fitted to the screen (decoded larger
    // the first time), and how to close it.
    if (const float shown = smoothFade(std::clamp(chat.viewer_motion, 0.0f, 1.0f)); shown > 0.003f && view->photo) {
        if (companion_photo_large_.pixels != view->photo) { companion_photo_large_ = {}; companion_photo_large_.pixels = view->photo; companion_photo_large_.short_edge = 1440; }
        if (!companion_photo_large_.bitmap) createBitmap(companion_photo_large_);
        auto& image = companion_photo_large_.bitmap ? companion_photo_large_ : companion_photo_;
        brush(gray(0, 0.84f * shown)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
        const auto area = layout.viewer();
        if (image.bitmap) {
            const auto size = image.bitmap->GetSize();
            if (size.width > 0 && size.height > 0) {
                const float fit = std::min((area.right - area.left) / size.width, (area.bottom - area.top) / size.height) * (0.94f + 0.06f * shown);
                const float w = size.width * fit, h = size.height * fit, mid_x = (area.left + area.right) * 0.5f, mid_y = (area.top + area.bottom) * 0.5f;
                const auto rect = D2D1::RectF(mid_x - w * 0.5f, mid_y - h * 0.5f, mid_x + w * 0.5f, mid_y + h * 0.5f);
                fillImage(image, rect, 18, shown);
                brush(gray(1, 0.2f * shown)); target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, 18, 18), solid_.Get(), 1.2f);
            }
        }
        textLine(tr(L"Kliknij, aby zamknąć"), area.left, area.bottom + 34, area.right - area.left, friend_status_font_.Get(), gray(0.72f, shown),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (const float shown = smoothFade(std::clamp(chat.mind_motion, 0.0f, 1.0f)); shown > 0.003f)
        drawCompanionMind(layout, chat, shown);
}

void Renderer::drawCompanionResetDialog(const companion_ui::Layout& layout, const companion_ui::Chat& chat) {
    const auto* view = context_->companion;
    drawCompanionQuestion(layout.confirm(), std::wstring(tr(L"Zresetować sympatię ")) + (view ? view->name : std::wstring{}) + L"?",
                          tr(L"Jej poziom wróci do 1, a pasek sympatii do zera. Tego nie da się cofnąć."), tr(L"Zresetuj"),
                          chat.confirm_focus, chat.hover);
}

void Renderer::drawCompanionQuestion(settings_ui::Rect area, const std::wstring& title, const std::wstring& body,
                                     const std::wstring& action, int focus, int hover, float alpha) {
    using companion_ui::confirm_reset, companion_ui::confirm_cancel, companion_ui::confirmFocus;
    const auto dialog = box(area);
    const float radius = desktop_style_ ? 14.0f : 22.0f;
    const auto shape = D2D1::RoundedRect(dialog, radius, radius);
    const auto glass = glassPanel(shape); // The scene before the dim, frosted in the panel.
    brush(gray(0, 0.6f * alpha)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    glassFill(glass, D2D1::ColorF(0.04f, 0.042f, 0.05f, 0.96f), alpha);
    brush(gray(1, (panel_borders_ ? 0.3f : 0.14f) * alpha)); target_->DrawRoundedRectangle(shape, solid_.Get(), 1.2f);
    constexpr float padding = companion_ui::Layout::confirm_padding;
    const float width = dialog.right - dialog.left - 2 * padding;
    textLine(title, dialog.left + padding, dialog.top + padding + 16, width, category_font_.Get(), gray(0.97f, alpha));
    paragraph(body, dialog.left + padding, dialog.top + padding + 46, width, settings_body_font_.Get(), gray(0.68f, alpha), 2);
    // The action: a red pill (brighter, with a white rim, when focused); Anuluj: an ordinary button.
    const auto act = box(companion_ui::Layout::dialogButton(area, confirm_reset));
    const auto act_shape = desktop_style_ ? D2D1::RoundedRect(act, 8, 8) : pill(act);
    const bool act_focus = focus == confirm_reset, act_hover = hover == confirmFocus(confirm_reset);
    brush(palette::dangerStrong((act_focus ? 1.0f : act_hover ? 0.88f : 0.72f) * alpha)); target_->FillRoundedRectangle(act_shape, solid_.Get());
    if (act_focus) { brush(gray(1, 0.95f * alpha)); target_->DrawRoundedRectangle(act_shape, solid_.Get(), 2.4f); }
    textLine(action, act.left, (act.top + act.bottom) * 0.5f, act.right - act.left, settings_body_font_.Get(), gray(1, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    const auto cancel = box(companion_ui::Layout::dialogButton(area, confirm_cancel));
    const auto ink = drawButton(cancel, focus == confirm_cancel ? 1.0f : 0.0f, alpha, false, hover == confirmFocus(confirm_cancel));
    textLine(tr(L"Anuluj"), cancel.left, (cancel.top + cancel.bottom) * 0.5f, cancel.right - cancel.left, settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
}

float Renderer::drawCompanionRoulette(const companion_ui::Roulette& roulette, float left, float bottom, float width, float alpha) {
    constexpr float height = 112, padding = 18, window_top = 44, window_height = 50;
    const auto rect = D2D1::RectF(left, bottom - height, left + width, bottom);
    const bool landed = roulette.landed();
    const auto shape = D2D1::RoundedRect(rect, 18, 18);
    brush(D2D1::ColorF(0.13f, 0.14f, 0.18f, 0.82f * alpha)); target_->FillRoundedRectangle(shape, solid_.Get());
    brush(landed ? uiAccent(0.8f * alpha) : gray(1, 0.12f * alpha)); target_->DrawRoundedRectangle(shape, solid_.Get(), landed ? 1.6f : 1.0f);
    textLine(landed ? tr(L"Ruletka wybrała") : tr(L"Ruletka się kręci…"), rect.left + padding, rect.top + 24, width - 2 * padding,
             friend_status_font_.Get(), palette::muted());
    // The slot window: titles roll upwards through it, slowing down onto the pick.
    const auto window = D2D1::RectF(rect.left + padding, rect.top + window_top, rect.right - padding, rect.top + window_top + window_height);
    brush(gray(0, 0.28f * alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(window, 12, 12), solid_.Get());
    target_->PushAxisAlignedClip(window, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float position = roulette.position();
    const auto first = static_cast<size_t>(position);
    const float offset = position - static_cast<float>(first);
    const float middle = (window.top + window.bottom) * 0.5f;
    for (size_t i = first; i <= first + 1 && i < roulette.reel.size(); ++i) {
        const float y = middle + (static_cast<float>(i) - static_cast<float>(first) - offset) * window_height;
        const float near_center = 1 - std::min(1.0f, std::abs(y - middle) / window_height);
        const auto ink = landed && i + 1 == roulette.reel.size() ? uiAccent(alpha) : gray(0.97f, alpha * (0.35f + 0.65f * near_center));
        textLine(roulette.reel[i], window.left + 16, y, window.right - window.left - 32, category_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    target_->PopAxisAlignedClip();
    return height;
}
