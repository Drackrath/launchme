// Flix home layout (src/app/flix.cpp): the focused game's background, its logo above the
// resting first row, then the rows of capsules and posters with their titles.
#include "renderer.hpp"
#include "flix_layout.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
float smooth(float value) { const float t = std::clamp(value, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
constexpr float tile_radius = 12;
}

void Renderer::drawFlix() {
    const auto& cards = *cards_;
    const auto& rows = context_->flix_rows;
    const float width = viewport_.width, height = viewport_.height;
    drawBackground();
    // The page rises as a lower row takes focus: the background dims under the posters.
    const float first_top = rows.empty() || rows[0].begin >= cards.size() ? 0.0f : cards[rows[0].begin].y - flix_layout::header;
    const float scale = context_->flix_scale;
    const float lift = flix_layout::lift(first_top, height, scale);
    brush(D2D1::ColorF(0.008f, 0.011f, 0.015f, 0.18f + 0.55f * smooth(lift)));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    beginScreenForeground();
    // The focused game's logo (or title) above the resting first row.
    const float rest_alpha = (1.0f - smooth(lift * 3.0f)) * (1.0f - smooth(context_->quick_progress)) * smooth(detail_) *
                             (1.0f - smooth(context_->category_menu_progress));
    if (rest_alpha > 0.01f && selected_ < cards.size() && !gameKey(cards[selected_].game).empty()) {
        const float bottom = first_top - 34;
        const auto& logo = logos_[selected_];
        if (logo.bitmap) {
            const auto size = logo.bitmap->GetPixelSize();
            const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
            const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
            const float factor = std::min(520.0f / ink_w, 170.0f / ink_h);
            target_->DrawBitmap(logo.bitmap.Get(), D2D1::RectF(flix_layout::left, bottom - ink_h * factor, flix_layout::left + ink_w * factor, bottom),
                                rest_alpha * reveal(logo), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        } else if (!context_->logo_pending && !logo.decoding)
            text(cards[selected_].game.name, flix_layout::left, bottom - 80, width - 2 * flix_layout::left, 80, hero_font_.Get(), gray(0.95f, rest_alpha));
    }
    drawQuickShade();
    const auto measure = [&](const std::wstring& value) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), category_font_.Get(), width, 48, layout.GetAddressOf())) &&
            SUCCEEDED(layout->GetMetrics(&metrics))) return metrics.widthIncludingTrailingWhitespace;
        return 0.0f;
    };
    const float focus_zone = flix_layout::focus_top + flix_layout::header + 60;
    for (size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (row.begin >= cards.size() || !row.count) continue;
        const float tile_top = cards[row.begin].y, tile_height = flix_layout::tileHeight(row.landscape, scale);
        if (tile_top > height + 10 || tile_top + tile_height < -flix_layout::header) continue;
        // Rows scrolled above the focused one fade away under the top edge.
        const float alpha = smooth((tile_top + tile_height - 30) / focus_zone);
        if (alpha < 0.01f) continue;
        const bool focused_row = r == context_->flix_row;
        const float header_y = tile_top - flix_layout::header;
        textLine(row.title, flix_layout::left, header_y + 22, width * 0.5f, category_font_.Get(), gray(focused_row ? 0.93f : 0.72f, alpha));
        if (focused_row && selected_ < cards.size() && isGame(cards[selected_].game)) {
            const float offset = flix_layout::left + std::min(width * 0.5f, measure(row.title)) + 14;
            brush(uiAccent(0.8f * alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(offset, header_y + 22), 2.8f, 2.8f), solid_.Get());
            textLine(cards[selected_].game.name, offset + 16, header_y + 22, width - offset - 16 - flix_layout::right_margin,
                     category_font_.Get(), gray(0.98f, alpha));
        }
        for (size_t i = row.begin; i < row.begin + row.count && i < cards.size(); ++i)
            if (i != selected_) drawRectangularCard(cards[i], i, alpha);
        if (focused_row && selected_ >= row.begin && selected_ < row.begin + row.count) {
            const float focus = smooth(context_->launch_progress);
            if (focus > 0.001f) {
                brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, focus * 0.82f));
                target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
            }
            drawRectangularCard(cards[selected_], selected_, alpha);
        }
    }
    // More rows below the resting first one: a small chevron under it.
    if (rows.size() > 1 && rest_alpha > 0.01f) {
        const float cx = width * 0.5f, cy = height - 18;
        line(cx - 9, cy - 4, cx, cy + 3, gray(0.85f, 0.6f * rest_alpha), 2.0f);
        line(cx, cy + 3, cx + 9, cy - 4, gray(0.85f, 0.6f * rest_alpha), 2.0f);
    }
}

void Renderer::drawRectangularCard(const GameCard& card, size_t index, float alpha) {
    const float w = card.width(), h = card.size;
    if (card.opacity < 0.01f || card.x > viewport_.width + 20 || card.x + w < -20 || card.y > viewport_.height + 20 || card.y + h < -20) return;
    const bool focused = index == selected_ && (!context_->library_controls ||
        (context_->library_controls->focus < 0 && context_->library_controls->action < 0 && !context_->library_controls->open()));
    // The quick bar takes the focus: the focused tile settles to the others' size and loses its
    // outline while the bar is open, and comes back as it closes.
    // `card.emphasis` eases toward the selection (src/app/launcher.cpp), so a new focus grows its
    // tile while the previous one settles back, instead of both snapping in one frame.
    // Landscape capsules fill their tile (Kinowy, Przegląd, Siatka's and the search's landscape tiles).
    const bool cinematic = (library_view::landscape(context_->library_mode) && context_->screen == ScreenMode::library) ||
        (gridScreen(context_->screen) && context_->library_mode == library_view::View::grid && context_->library_options.grid_landscape);
    const bool xmb = context_->xmb && !cinematic;
    const float emphasis = xmb ? (focused ? 1.0f - smooth(context_->quick_progress) : 0.0f) : card.emphasis;
    const float launch = focused ? smooth(context_->launch_progress) : 0.0f;
    const float scale = xmb ? 1.0f + 0.13f * launch : 1.0f + 0.045f * emphasis + 0.08f * launch;
    const float tile_w = w * scale, tile_h = h * scale;
    const float x = card.x - (tile_w - w) * 0.5f, y = card.y - (tile_h - h) * 0.5f;
    // The tile's own edge: shadow, focus outline and plain edge.
    const auto frame_rect = D2D1::RectF(x, y, x + tile_w, y + tile_h);
    const auto frame_shape = tileShape(frame_rect, tile_shape_);
    if (!frame_shape) return;
    // "Grafika do środka" (focus gap inside): the focused picture shrinks inside its outline, an empty
    // band between them; everything else fills the whole tile.
    const float inner = focused ? focusInset(3.2f) * emphasis : 0.0f;
    const auto rect = inner > 0.05f ? D2D1::RectF(x + inner, y + inner, x + tile_w - inner, y + tile_h - inner) : frame_rect;
    ComPtr<ID2D1Geometry> shape = frame_shape;
    if (inner > 0.05f) if (auto inside = tileShape(rect, tile_shape_)) shape = inside;
    const float transparency = card.opacity * alpha * (context_->xmb ? 1.0f : 0.9f + 0.1f * emphasis);
    const auto& game = card.game;
    for (int layer = 3; layer >= 1; --layer) {
        const float spread = layer * (3.0f + 2.0f * emphasis);
        brush(D2D1::ColorF(0, 0, 0, (0.045f + 0.025f * emphasis) * transparency));
        const auto shadow = tileShape(D2D1::RectF(x - spread, y + 8 - spread, x + tile_w + spread, y + tile_h + 8 + spread), tile_shape_);
        if (shadow) target_->FillGeometry(shadow.Get(), solid_.Get());
    }
    const bool platform_tile = game.kind == GameKind::platform || game.kind == GameKind::pc_platform;
    constexpr float palette[4][3] = {{0.13f, 0.21f, 0.24f}, {0.20f, 0.20f, 0.30f}, {0.28f, 0.20f, 0.17f}, {0.15f, 0.24f, 0.20f}};
    const int tone = static_cast<int>(game.app_id % 4);
    if (inner > 0.05f) { // The empty band: almost black, under the picture.
        brush(D2D1::ColorF(0.012f, 0.014f, 0.018f, 0.94f * transparency));
        target_->FillGeometry(frame_shape.Get(), solid_.Get());
    }
    brush(platform_tile ? platformTileFill(game, transparency) :
          cinematic ? D2D1::ColorF(0.045f, 0.05f, 0.06f, transparency) :
          isGame(game) ? D2D1::ColorF(palette[tone][0], palette[tone][1], palette[tone][2], transparency) :
          D2D1::ColorF(0.07f, 0.08f, 0.09f, 0.94f * transparency));
    target_->FillGeometry(shape.Get(), solid_.Get());
    const auto session = sessionOf(game);
    const bool frozen = session == SessionState::frozen;
    Cover* poster = index < posters_.size() && posters_[index].bitmap ? &posters_[index] : nullptr;
    Cover* cover = index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    D2D1_COLOR_F accent = cover_accent_ ? D2D1::ColorF(((cover_accent_ >> 16) & 255) / 255.0f, ((cover_accent_ >> 8) & 255) / 255.0f, (cover_accent_ & 255) / 255.0f) :
                          poster ? poster->accent : cover ? cover->accent : D2D1::ColorF(0.94f, 0.96f, 0.97f);
    if (frozen) accent = D2D1::ColorF(0.80f, 0.82f, 0.84f);
    if (platform_tile) {
        if (cover) {
            // Store and console logos, fitted inside the tile.
            const auto pixels = cover->bitmap->GetPixelSize();
            const auto source = cover->has_ink_bounds ? cover->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
            const float factor = std::min(tile_w * 0.7f / (source.right - source.left), tile_h * 0.4f / (source.bottom - source.top));
            const float lw = (source.right - source.left) * factor, lh = (source.bottom - source.top) * factor;
            target_->DrawBitmap(cover->bitmap.Get(), D2D1::RectF(x + (tile_w - lw) * 0.5f, y + (tile_h - lh) * 0.5f, x + (tile_w + lw) * 0.5f, y + (tile_h + lh) * 0.5f),
                                transparency * card.cover_opacity * reveal(*cover), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        } else if (!covers_[index].decoding)
            text(game.name, x + 12, y + tile_h * 0.5f - 24, tile_w - 24, 48, category_font_.Get(), gray(0.94f, transparency));
    } else if (game.kind == GameKind::all || game.kind == GameKind::category) {
        // "Wszystkie gry" and a row's "Wszystkie": the quick bar's 2x2 glyph and the caption under
        // it; a row's tile sits on a random background of its category, dimmed for the caption.
        if (poster) {
            fillImage(*poster, rect, tile_radius, transparency * reveal(*poster), false, shape.Get());
            brush(D2D1::ColorF(0.008f, 0.011f, 0.015f, 0.46f * transparency * reveal(*poster)));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
        const float cx = x + tile_w * 0.5f, cy = y + tile_h * 0.44f, cell = std::min(tile_w, tile_h) * 0.11f, gap = cell * 0.3f;
        brush(gray(0.94f, 0.9f * transparency));
        for (int tile = 0; tile < 4; ++tile) {
            const float left = tile % 2 ? cx + gap * 0.5f : cx - gap * 0.5f - cell, top = tile / 2 ? cy + gap * 0.5f : cy - gap * 0.5f - cell;
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, left + cell, top + cell), cell * 0.25f, cell * 0.25f), solid_.Get(), 2.2f);
        }
        text(game.name, x + 12, cy + cell + 18, tile_w - 24, 30, button_font_.Get(), gray(0.9f, transparency));
    } else if (poster || cover) {
        // A stand-in cover of another shape (square, or a wide header) would lose its sides and often
        // its title to the tile's crop: shown whole over a dimmed fill of itself instead.
        const auto standIn = [&](Cover& image, float opacity) {
            const auto pixels = image.bitmap->GetPixelSize();
            const float ratio = static_cast<float>(pixels.width) / static_cast<float>(std::max(1u, pixels.height));
            if (cinematic || std::abs(ratio / (tile_w / tile_h) - 1) < 0.12f) {
                fillImage(image, rect, tile_radius, opacity, frozen, shape.Get(), cinematic); return;
            }
            fillImage(image, rect, tile_radius, opacity, frozen, shape.Get());
            brush(D2D1::ColorF(0.008f, 0.011f, 0.015f, 0.62f * opacity)); target_->FillGeometry(shape.Get(), solid_.Get());
            fillImage(image, rect, tile_radius, opacity, frozen, shape.Get(), true);
        };
        const float shown = poster ? reveal(*poster) : 0.0f;
        // The poster fades in over the cover it replaces, never over the bare tile.
        if (cover && shown < 1) standIn(*cover, transparency * card.cover_opacity * reveal(*cover));
        if (poster) fillImage(*poster, rect, tile_radius, transparency * shown, frozen, shape.Get(), cinematic);
    } else if (!(index < posters_.size() && posters_[index].decoding) && !(index < covers_.size() && covers_[index].decoding)) {
        text(game.name, x + 14, y + tile_h * 0.5f - 24, tile_w - 28, 48, button_font_.Get(), gray(0.92f, transparency));
    }
    if (isGame(game)) {
        if (frozen) {
            brush(D2D1::ColorF(0.015f, 0.018f, 0.022f, 0.34f * transparency));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
        if (!game.installed && game.kind != GameKind::catalog) {
            brush(D2D1::ColorF(0, 0, 0, 0.32f * transparency));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
    }
    if (game.installation.active() || session != SessionState::none)
        drawCardStatusShade(x, y, tile_w, tile_h, transparency, shape.Get());
    // The accent outline fades into the plain edge of the other tiles as `emphasis` drops; with the
    // focus gap it stands an empty ring away from the tile.
    const float ring = context_->focus_ring ? 0.0f : emphasis;
    ComPtr<ID2D1Geometry> outline = frame_shape;
    if (ring > 0.004f) if (const float out = focusOutset(3.2f); out > 0)
        if (auto wider = tileShape(D2D1::RectF(frame_rect.left - out, frame_rect.top - out, frame_rect.right + out, frame_rect.bottom + out), tile_shape_)) outline = wider;
    if (ring > 0.004f && glow_trace_) {
        drawOrbitOutline(outline.Get(), accent, ring * transparency);
    } else if (ring > 0.004f) {
        const float pulse = glowPulse();
        if (glowAlpha() > 0.004f) {
            brush(D2D1::ColorF(accent.r, accent.g, accent.b, std::min(1.0f, ring * transparency * glowAlpha() * (0.07f + pulse * 0.09f))));
            target_->DrawGeometry(outline.Get(), solid_.Get(), glowWidth(13.0f));
        }
        brush(D2D1::ColorF(accent.r, accent.g, accent.b, ring * transparency * (0.73f + pulse * 0.27f)));
        target_->DrawGeometry(outline.Get(), solid_.Get(), 3.2f);
    }
    if (ring < 0.996f) {
        brush(gray(1, (1.0f - ring) * 0.16f * transparency));
        target_->DrawGeometry(frame_shape.Get(), solid_.Get(), 1.0f);
    }
    // Experimental spring focus: the outline on its own rectangle, zoomed like the tile.
    if (focused && context_->focus_ring && emphasis > 0.004f) drawFocusRing(accent, emphasis * transparency, scale, false);
    if (game.installation.active()) {
        const auto& install = game.installation;
        const float radius = std::clamp(std::min(tile_w, tile_h) * 0.064f, 11.0f, 22.0f);
        drawInstallDisc(x + tile_w - radius - 17, y + radius + 17, radius, transparency, install.moving());
        const auto percent = install.percent();
        drawCardStatus(x, y, tile_w, tile_h, transparency, installationText(install), percent ? *percent / 100.0f : 0.0f, uiAccent(transparency));
    } else if (session != SessionState::none) {
        drawCardBadge(x, y, tile_w, tile_h, transparency, frozen);
        drawCardStatus(x, y, tile_w, tile_h, transparency, frozen ? tr(L"Wstrzymana") : tr(L"W grze"));
    } else if (emphasis > 0.004f && isGame(game) && !game.installed && game.kind != GameKind::catalog && !cinematic) // Library tiles: the dimming says it.
        text(localFileGame(game.source) ? tr(L"BRAK PLIKU") : tr(L"DO INSTALACJI"), x + 8, y + tile_h - 36, tile_w - 16, 28,
             button_font_.Get(), gray(0.95f, emphasis * transparency));
}
