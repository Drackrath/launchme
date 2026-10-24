#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
using palette::gray;
D2D1_RECT_F rect(settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
float middle(settings_ui::Rect r) { return (r.top + r.bottom) * 0.5f; }
}

void Renderer::setFileBrowserPreview(std::vector<uint8_t> bytes) {
    file_browser_preview_ = {};
    file_browser_preview_.hero = true; // Whole picture, no crop or poster accent.
    if (!bytes.empty()) {
        file_browser_preview_.pixels = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
        createBitmap(file_browser_preview_);
    }
}

// Stroked like the other glyphs of the launcher: 1.8 px lines centred on (cx, cy), ~24 px wide.
void Renderer::drawFileGlyph(file_browser::Kind kind, float cx, float cy, D2D1_COLOR_F ink) {
    using file_browser::Kind;
    brush(ink);
    switch (kind) {
    case Kind::up:
        line(cx, cy + 9, cx, cy - 9, ink, 1.8f);
        line(cx - 6, cy - 3, cx, cy - 9, ink, 1.8f);
        line(cx + 6, cy - 3, cx, cy - 9, ink, 1.8f);
        break;
    case Kind::drive:
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 12, cy - 7, cx + 12, cy + 7), 3, 3), solid_.Get(), 1.8f);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 6, cy), 1.8f, 1.8f), solid_.Get());
        break;
    case Kind::folder:
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 12, cy - 6, cx + 12, cy + 9), 2.5f, 2.5f), solid_.Get(), 1.8f);
        line(cx - 12, cy - 5, cx - 12, cy - 9, ink, 1.8f);
        line(cx - 12, cy - 9, cx - 4, cy - 9, ink, 1.8f);
        line(cx - 4, cy - 9, cx - 1, cy - 6, ink, 1.8f);
        break;
    case Kind::file:
        line(cx - 8, cy - 11, cx + 3, cy - 11, ink, 1.8f);
        line(cx + 3, cy - 11, cx + 8, cy - 6, ink, 1.8f);
        line(cx + 8, cy - 6, cx + 8, cy + 11, ink, 1.8f);
        line(cx + 8, cy + 11, cx - 8, cy + 11, ink, 1.8f);
        line(cx - 8, cy + 11, cx - 8, cy - 11, ink, 1.8f);
        line(cx + 3, cy - 11, cx + 3, cy - 6, ink, 1.4f);
        line(cx + 3, cy - 6, cx + 8, cy - 6, ink, 1.4f);
        break;
    }
}

void Renderer::drawPlaceGlyph(file_browser::Icon icon, float cx, float cy, D2D1_COLOR_F ink) {
    using file_browser::Icon;
    brush(ink);
    const auto box = [&](float left, float top, float right, float bottom, float radius = 2.5f) {
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + left, cy + top, cx + right, cy + bottom), radius, radius), solid_.Get(), 1.8f);
    };
    switch (icon) {
    case Icon::computer:
    case Icon::desktop:
        box(-11, -9, 11, 6);
        line(cx, cy + 6, cx, cy + 10, ink, 1.8f);
        line(cx - 6, cy + 10, cx + 6, cy + 10, ink, 1.8f);
        if (icon == Icon::desktop) target_->FillRectangle(D2D1::RectF(cx - 8, cy - 6, cx - 3, cy - 1), solid_.Get());
        break;
    case Icon::recent:
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 10, 10), solid_.Get(), 1.8f);
        line(cx, cy, cx, cy - 6, ink, 1.8f);
        line(cx, cy, cx + 5, cy + 3, ink, 1.8f);
        break;
    case Icon::downloads:
        line(cx, cy - 10, cx, cy + 4, ink, 1.8f);
        line(cx - 6, cy - 2, cx, cy + 4, ink, 1.8f);
        line(cx + 6, cy - 2, cx, cy + 4, ink, 1.8f);
        line(cx - 10, cy + 10, cx + 10, cy + 10, ink, 1.8f);
        break;
    case Icon::documents: drawFileGlyph(file_browser::Kind::file, cx, cy, ink); break;
    case Icon::pictures:
        box(-11, -8, 11, 8);
        line(cx - 8, cy + 5, cx - 2, cy - 1, ink, 1.6f);
        line(cx - 2, cy - 1, cx + 2, cy + 3, ink, 1.6f);
        line(cx + 2, cy + 3, cx + 5, cy, ink, 1.6f);
        line(cx + 5, cy, cx + 8, cy + 5, ink, 1.6f);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 5, cy - 4), 1.8f, 1.8f), solid_.Get());
        break;
    case Icon::music:
        line(cx - 2, cy + 6, cx - 2, cy - 9, ink, 1.8f);
        line(cx - 2, cy - 9, cx + 8, cy - 6, ink, 1.8f);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 6, cy + 7), 4.2f, 3.4f), solid_.Get());
        break;
    case Icon::videos:
        box(-11, -8, 11, 8);
        line(cx - 3, cy - 4, cx - 3, cy + 4, ink, 1.6f);
        line(cx - 3, cy - 4, cx + 4, cy, ink, 1.6f);
        line(cx - 3, cy + 4, cx + 4, cy, ink, 1.6f);
        break;
    case Icon::steam:
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 10, 10), solid_.Get(), 1.8f);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 3, cy - 2), 3.5f, 3.5f), solid_.Get(), 1.6f);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 4, cy + 4), 2.2f, 2.2f), solid_.Get());
        line(cx - 4, cy + 4, cx + 1, cy, ink, 1.6f);
        break;
    case Icon::folder: drawFileGlyph(file_browser::Kind::folder, cx, cy, ink); break;
    }
}

// Full screen, in the Settings layout: places on the rail, Back under them, the open folder's
// entries as rows, an image preview on the right and the action buttons along the bottom.
void Renderer::drawFileBrowser() {
    if (!context_->file_browser || !context_->file_browser->open) return;
    using namespace file_browser;
    const auto& ui = *context_->file_browser;
    const Layout layout{viewport_.width, viewport_.height, ui.request.images};
    const auto base = layout.base();
    const auto body = settings_body_font_.Get();
    const auto fill = [&](settings_ui::Rect r, D2D1_COLOR_F ink, float radius = 12) {
        brush(ink);
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(r), radius, radius), solid_.Get());
    };
    // Rail places and list rows: pills without an outline.
    const auto fillPill = [&](settings_ui::Rect r, D2D1_COLOR_F ink) {
        brush(ink);
        target_->FillRoundedRectangle(pill(rect(r)), solid_.Get());
    };
    const auto chevron = [&](float x, float y, D2D1_COLOR_F ink) {
        line(x - 4, y - 6, x + 2, y, ink, 1.8f);
        line(x + 2, y, x - 4, y + 6, ink, 1.8f);
    };

    brush(D2D1::ColorF(0.022f, 0.024f, 0.030f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const auto heading = settings_heading_font_.Get();
    textGroup(tr(L"Miejsca"), {}, 62, settings_ui::Layout::heading_cap_top, base.railRight() - 68, heading, gray(0.96f), body, gray(0.55f), 1, true);
    const std::wstring where = ui.root() ? std::wstring(tr(L"Ten komputer")) : ui.folder.native();
    textGroup(ui.request.title, where, layout.listLeft(), settings_ui::Layout::heading_cap_top, base.contentRight() - layout.listLeft(),
              heading, gray(0.96f), body, gray(0.55f), 1, true, heading_group_gap);

    // Rail: a solid white pill on the focused place (or Back), a faint one on the open folder's place.
    const int active = ui.activePlace();
    const int shown_places = std::min(static_cast<int>(ui.places.size()), layout.placeCapacity());
    if (active >= 0 && active < shown_places && !(ui.focus == Focus::places && ui.place == active))
        fillPill(layout.place(active), gray(1, 0.07f));
    if (ui.focus == Focus::places && ui.place >= 0 && ui.place < shown_places) fillPill(layout.place(ui.place), gray(0.91f));
    for (int i = 0; i < shown_places; ++i) {
        const auto r = layout.place(i);
        const bool solid = ui.focus == Focus::places && ui.place == i;
        const auto ink = solid ? gray(0.08f) : gray(i == active ? 0.96f : 0.62f);
        drawPlaceGlyph(ui.places[i].icon, r.left + 34, middle(r), ink);
        textLine(ui.places[i].label, r.left + 62, middle(r), r.right - r.left - 74, category_font_.Get(), ink);
    }
    const auto back = layout.back();
    const bool back_focus = ui.focus == Focus::back;
    brush(drawButton(rect(back), back_focus ? 1.0f : 0.0f, 1, false));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, middle(back)), 4, 4), solid_.Get());

    // Rows.
    const float list_top = layout.listTop(), list_bottom = layout.listBottom();
    const float left = layout.listLeft(), right = layout.listRight();
    target_->PushAxisAlignedClip(D2D1::RectF(left - 3, list_top - 2, right + 3, list_bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const int count = static_cast<int>(ui.entries.size());
    const int first = std::max(0, static_cast<int>(ui.scroll / Layout::row_step));
    const int last = std::min(count, first + layout.visibleRows() + 2);
    for (int i = first; i < last; ++i) {
        const auto& entry = ui.entries[i];
        const auto r = layout.row(i, ui.scroll);
        const float y = middle(r);
        const bool selected = i == ui.selected;
        fillPill(r, gray(1, selected ? (ui.focus == Focus::list ? 0.12f : 0.07f) : ui.hover == i ? 0.05f : 0.034f));
        const float shade = entry.hidden ? 0.55f : 0.93f;
        drawFileGlyph(entry.kind, r.left + 36, y, entry.kind == Kind::up ? gray(0.75f) : entry.kind == Kind::file ? gray(shade * 0.85f) : gray(shade));
        const float detail_width = entry.detail.empty() ? 0.0f : std::min(300.0f, (r.right - r.left) * 0.34f);
        const float name_right = r.right - 24 - (detail_width > 0 ? detail_width + 16 : 0) - (entry.kind == Kind::file ? 0 : 24);
        textLine(entry.name, r.left + 66, y, name_right - r.left - 66, category_font_.Get(), gray(entry.kind == Kind::up ? 0.80f : shade));
        if (detail_width > 0)
            textLine(entry.detail, r.right - 24 - (entry.kind == Kind::file ? 0 : 24) - detail_width, y, detail_width, body, gray(0.52f), DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (entry.kind == Kind::folder || entry.kind == Kind::drive) chevron(r.right - 26, y, gray(selected ? 0.85f : 0.55f));
    }
    if (!ui.error.empty()) {
        // Under "Folder nadrzędny" (or at the top of the drive list).
        const auto r = layout.row(ui.root() ? 0 : 1, ui.scroll);
        textLine(ui.error, r.left + 24, middle(r), r.right - r.left - 48, body, gray(0.55f));
    }
    target_->PopAxisAlignedClip();
    const float maximum = layout.maxScroll(count);
    if (maximum > 0) {
        const float height = list_bottom - list_top;
        const float thumb = std::max(30.0f, height * height / (maximum + height));
        const float y = list_top + (height - thumb) * std::clamp(ui.scroll / maximum, 0.0f, 1.0f);
        fill({right + 9, list_top, right + 12, list_bottom}, gray(1, 0.06f), 1.5f);
        fill({right + 9, y, right + 12, y + thumb}, gray(1, 0.35f), 1.5f);
    }

    if (ui.request.images) {
        const auto box = layout.previewBox();
        fill(box, gray(1, 0.034f));
        auto& image = file_browser_preview_;
        if (image.bitmap) fillImage(image, D2D1::RectF(box.left + 16, box.top + 16, box.right - 16, box.bottom - 16), 8, 1, false, nullptr, true);
        else if (!image.loaded())
            textLine(tr(L"Brak podglądu"), box.left + 16, middle(box), box.right - box.left - 32, body, gray(0.42f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Buttons along the bottom, beside Back.
    const auto buttons = ui.buttons();
    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        const auto r = layout.button(i, static_cast<int>(buttons.size()));
        const bool focused = ui.focus == Focus::buttons && ui.button == i;
        const bool enabled = buttons[i] != Button::choose_folder || !ui.root();
        const auto ink = drawButton(rect(r), focused ? 1.0f : 0.0f, 1, false, false, enabled);
        std::wstring label;
        switch (buttons[i]) {
        case Button::choose_folder: label = tr(L"Wybierz ten folder"); break;
        case Button::file_type: label = ui.all_files ? std::wstring(tr(L"Wszystkie pliki")) : ui.request.filter_label; break;
        case Button::hidden: label = ui.show_hidden ? tr(L"Ukryj ukryte pliki") : tr(L"Pokaż ukryte pliki"); break;
        case Button::system_dialog: label = tr(L"Okno systemowe…"); break;
        }
        textLine(label, r.left + 12, middle(r), r.right - r.left - 24, body, ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}
