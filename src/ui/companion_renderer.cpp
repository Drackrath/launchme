// Towarzyszka: her home tile (a medium close-up whose head rises above the tile, with a speech
// bubble; on Flix a pill, on Metro and XMB inside the layout's own tile), her chat screen and the
// look previews in Settings. Her pictures are the embedded sprites of res/companion/, rigged by
// avatar::Rig; geometry comes from companion_view.hpp and home_widgets.hpp.
#include "renderer.hpp"
#include "../net/companion_links.hpp"
#include "../platform/text_encoding.hpp"

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
D2D1_RECT_F box(settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
float smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
}

Renderer::CompanionText Renderer::companionText(const std::wstring& value, const std::vector<companion::GameMention>& mentions,
                                               float width, int max_lines) {
    CompanionText result;
    auto* font = settings_body_font_.Get();
    if (value.empty() || width <= 0 || !font) return result;
    const float max_height = typeMetrics(font).line * max_lines;
    if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), font, width, max_height,
                                         result.layout.GetAddressOf()))) return result;
    result.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    result.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    for (const auto& mention : mentions) {
        if (!mention.length || mention.at >= value.size()) continue;
        result.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, {static_cast<UINT32>(mention.at),
            static_cast<UINT32>(std::min(mention.length, value.size() - mention.at))});
    }
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(result.layout->GetMetrics(&metrics))) {
        // Measure with the bold runs already applied: wrapping, bubbles and hit boxes agree.
        result.width = metrics.lineCount > 1 ? width : std::min(width, std::max(24.0f, metrics.widthIncludingTrailingWhitespace + 2));
        result.height = std::min(max_height, metrics.height);
        result.layout->SetMaxWidth(result.width);
    }
    return result;
}

void Renderer::drawCompanionText(const CompanionText& text, const std::wstring& value, const std::vector<companion::GameMention>& mentions,
                                  float x, float y, D2D1_COLOR_F ink, D2D1_RECT_F clip, companion::Look speaker) {
    if (!text.layout) return;
    ComPtr<ID2D1SolidColorBrush> link_ink, hover_ink;
    const auto tint = companion_ui::tint(speaker);
    target_->CreateSolidColorBrush(D2D1::ColorF(tint.r, tint.g, tint.b, ink.a), link_ink.GetAddressOf());
    target_->CreateSolidColorBrush(D2D1::ColorF(0.3f + 0.7f * tint.r, 0.3f + 0.7f * tint.g, 0.3f + 0.7f * tint.b, ink.a), hover_ink.GetAddressOf());
    for (const auto& mention : mentions) {
        if (!mention.length || mention.at >= value.size()) continue;
        const DWRITE_TEXT_RANGE range{static_cast<UINT32>(mention.at), static_cast<UINT32>(std::min(mention.length, value.size() - mention.at))};
        if (link_ink) text.layout->SetDrawingEffect(link_ink.Get(), range);
        // The typewriter may show only the start of a title: colour it, but wait before linking it.
        if (mention.length > value.size() - mention.at) continue;
        UINT32 count = 0;
        text.layout->HitTestTextRange(range.startPosition, range.length, x, y, nullptr, 0, &count);
        if (!count) continue;
        std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
        if (FAILED(text.layout->HitTestTextRange(range.startPosition, range.length, x, y, hits.data(), count, &count))) continue;
        bool hovered = false;
        for (const auto& hit : hits) {
            if (!hit.isText || hit.isTrimmed) continue;
            const settings_ui::Rect rect{std::max({hit.left, clip.left, x}), std::max({hit.top, clip.top, y}),
                std::min({hit.left + hit.width, clip.right, x + text.width}), std::min({hit.top + hit.height, clip.bottom, y + text.height})};
            if (rect.right > rect.left && rect.bottom > rect.top) {
                companion_links_.push_back({rect, value.substr(mention.at, mention.length)});
                if (context_->companion && context_->companion->link_pointer) {
                    const auto [px, py] = *context_->companion->link_pointer;
                    hovered = hovered || rect.contains(px, py);
                }
            }
        }
        if (hovered) {
            text.layout->SetUnderline(TRUE, range);
            if (hover_ink) text.layout->SetDrawingEffect(hover_ink.Get(), range);
        }
    }
    brush(ink);
    target_->DrawTextLayout(D2D1::Point2F(x, y), text.layout.Get(), solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

Renderer::Cover* Renderer::companionSprite(companion::Look look, companion_ui::Frame frame, companion::Emotion emotion, int costume) {
    return companionResource(companion_ui::spriteId(look, frame, emotion, std::max(costume, 0)));
}

Renderer::Cover* Renderer::companionResource(int id) {
    auto& sprite = companion_sprites_[id];
    if (sprite.missing) return nullptr;
    auto& cover = sprite.cover;
    if (!cover.loaded()) {
        // Embedded PNG (companion.rc, generated by CMake from res/companion/): mapped memory, copied once.
        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
        const HGLOBAL handle = resource ? LoadResource(module, resource) : nullptr;
        const auto* bytes = handle ? static_cast<const uint8_t*>(LockResource(handle)) : nullptr;
        const DWORD size = resource ? SizeofResource(module, resource) : 0;
        if (!bytes || !size) { sprite.missing = true; return nullptr; }
        cover.pixels = shareImage(std::vector<uint8_t>(bytes, bytes + size));
    }
    createBitmap(cover); // Decoded off the UI thread; uploaded when ready.
    return cover.bitmap ? &cover : nullptr;
}

void Renderer::drawCompanionFigure(companion::Look look, const avatar::Pose& pose, float x, float bottom, float height, float alpha, int costume) {
    if (alpha <= 0.003f) return;
    using companion_ui::Frame;
    // The frame of a feeling: closed eyes while blinking, an open mouth on syllables, else the
    // picture. A costume without that feeling shows its calm face (the outfit stays); one without
    // that either, her basic outfit's picture of it, else its calm face.
    const auto plain = [&](companion::Emotion emotion) {
        const auto attempts = costume ? std::array<std::pair<companion::Emotion, int>, 4>{{{emotion, costume}, {companion::calm, costume}, {emotion, 0}, {companion::calm, 0}}}
                                      : std::array<std::pair<companion::Emotion, int>, 4>{{{emotion, 0}, {companion::calm, 0}, {companion::calm, 0}, {companion::calm, 0}}};
        for (const auto& [feeling, slot] : attempts) {
            Cover* sprite = companionSprite(look, Frame::base, feeling, slot);
            if (sprite || !companion_sprites_[companion_ui::spriteId(look, Frame::base, feeling, slot)].missing) return sprite; // Still decoding: wait for it.
        }
        return static_cast<Cover*>(nullptr);
    };
    const auto pick = [&](companion::Emotion emotion) {
        Cover* sprite = nullptr;
        if (pose.blink) sprite = companionSprite(look, Frame::blink, emotion, costume);
        if (!sprite && pose.mouth) sprite = companionSprite(look, Frame::talk, emotion, costume);
        return sprite ? sprite : plain(emotion);
    };
    Cover* current = pick(pose.emotion);
    Cover* previous = pose.blend < 1 && pose.previous != pose.emotion ? plain(pose.previous) : nullptr;
    if (!current) { current = previous; previous = nullptr; } // A new picture still decoding: keep the last one.
    if (!current) return;
    const float w = height * companion_ui::sprite_aspect, unit = height / 300;
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const auto screen = D2D1::Matrix3x2F::ReinterpretBaseType(&base)[0];
    // Her face on this picture (faces.txt); unknown: where the square bust usually has it.
    const auto known = companion_ui::faceOf(look, costume, pose.emotion);
    const float per_pixel = height / companion_ui::sprite_height;
    const auto face = known.size > 0 ? D2D1::Point2F(x - w * 0.5f + known.x * per_pixel, bottom - height + known.y * per_pixel)
                                     : D2D1::Point2F(x, bottom - height * 0.64f);
    const float face_size = known.size > 0 ? known.size * per_pixel : height * 0.34f;
    // Her pictures are close-ups, so moving the picture moves her head: it breathes and squashes on
    // its bottom edge, tilts and nods about the neck (just below her chin), glances aside.
    const auto anchor = D2D1::Point2F(x, bottom);
    const auto neck = D2D1::Point2F(x, std::min(bottom, face.y + face_size * 0.6f));
    const auto transform = D2D1::Matrix3x2F::Scale(pose.scale_x, pose.scale_y, anchor) *
                           D2D1::Matrix3x2F::Scale(1, 1 - pose.head_nod * 0.0035f, neck) *
                           D2D1::Matrix3x2F::Rotation(pose.tilt + pose.head_tilt, neck) *
                           D2D1::Matrix3x2F::Translation((pose.x + pose.head_x) * unit, (pose.y + pose.head_nod * 0.6f) * unit);
    ComPtr<ID2D1DeviceContext> context;
    target_.As(&context);
    const auto rect = D2D1::RectF(x - w * 0.5f, bottom - height, x + w * 0.5f, bottom);
    target_->SetTransform(transform * screen);
    const auto blit = [&](Cover& cover, float opacity) {
        // High-quality cubic where available: line art keeps its edges when scaled down.
        if (context) context->DrawBitmap(cover.bitmap.Get(), rect, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        else target_->DrawBitmap(cover.bitmap.Get(), rect, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    // A quick cross-fade from the previous feeling.
    const float shown = previous ? std::clamp(pose.blend, 0.0f, 1.0f) : 1.0f;
    if (previous) blit(*previous, alpha * (1 - shown) / std::max(0.05f, 1 - alpha * shown));
    blit(*current, alpha * shown);
    drawCompanionMarks(pose, face, face_size, alpha); // Moving with her head.
    target_->SetTransform(base);
}

bool Renderer::drawCompanionPose(companion::Look look, int costume, settings_ui::Rect area, float alpha, float slide) {
    const int id = companion_ui::poseId(look, std::max(costume, 0));
    Cover* picture = companionResource(id);
    if (!picture) return !companion_sprites_[id].missing; // Decoding: her close-up waits for it.
    if (alpha <= 0.003f) return true;
    const auto size = picture->bitmap->GetSize();
    if (size.width <= 0 || size.height <= 0) return true;
    // As large as the column allows, standing on its bottom edge and centred across it; sitting or
    // lying poses are wider than tall and rest on the floor.
    const float room_w = area.right - area.left, room_h = area.bottom - area.top;
    const float scale = std::min(room_w / size.width, room_h / size.height);
    const float w = size.width * scale, h = size.height * scale;
    const float x = (area.left + area.right - w) * 0.5f - slide;
    const auto rect = D2D1::RectF(x, area.bottom - h, x + w, area.bottom);
    ComPtr<ID2D1DeviceContext> context;
    target_.As(&context);
    if (context) context->DrawBitmap(picture->bitmap.Get(), rect, alpha, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
    else target_->DrawBitmap(picture->bitmap.Get(), rect, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
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
// Her speech bubble: near-black frosted glass with white text, a thin light rim and a tail curving
// towards (tip_x, tip_y); typing shows three dots.
D2D1_RECT_F Renderer::drawSpeechBubble(const std::wstring& value, bool typing, float anchor, float center_y, float max_width,
                                       float tip_x, float tip_y, float alpha, float min_top, bool left_of_anchor) {
    auto* font = settings_body_font_.Get();
    const float padding_x = 20, padding_y = 14;
    const auto& mentions = context_->companion->bubble_mentions;
    const auto text = companionText(value, mentions, max_width - 2 * padding_x, 8);
    const float text_width = typing ? 54 : std::max(text.width, 28.0f);
    const float text_height = typing ? typeMetrics(font).line : text.height;
    const float w = text_width + 2 * padding_x, h = text_height + 2 * padding_y;
    const float top = std::max(min_top, center_y - h * 0.5f);
    const auto rect = left_of_anchor ? D2D1::RectF(anchor - w, top, anchor, top + h) : D2D1::RectF(anchor, top, anchor + w, top + h);
    // The bubble and its tail as one outline, so glass, tint and rim follow both.
    ComPtr<ID2D1RoundedRectangleGeometry> body;
    ComPtr<ID2D1PathGeometry> tail, shape;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, 20, 20), body.GetAddressOf())) ||
        FAILED(factory_->CreatePathGeometry(tail.GetAddressOf())) || FAILED(tail->Open(sink.GetAddressOf()))) return rect;
    const bool right_side = tip_x >= rect.right;
    const float edge = right_side ? rect.right - 12 : rect.left + 12;
    const float base_y = std::clamp(tip_y - 10, rect.top + 16, rect.bottom - 16);
    sink->BeginFigure(D2D1::Point2F(edge, base_y - 11), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(edge + (tip_x - edge) * 0.45f, base_y - 10), D2D1::Point2F(tip_x - (tip_x - edge) * 0.15f, tip_y - 6),
                                        D2D1::Point2F(tip_x, tip_y)));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(edge + (tip_x - edge) * 0.55f, base_y + 6), D2D1::Point2F(edge + (tip_x - edge) * 0.2f, base_y + 10),
                                        D2D1::Point2F(edge, base_y + 11)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return rect;
    sink.Reset();
    if (FAILED(factory_->CreatePathGeometry(shape.GetAddressOf())) || FAILED(shape->Open(sink.GetAddressOf())) ||
        FAILED(body->CombineWithGeometry(tail.Get(), D2D1_COMBINE_MODE_UNION, nullptr, sink.Get())) || FAILED(sink->Close())) return rect;
    // A soft drop shadow, then frosted near-black glass.
    D2D1_MATRIX_3X2_F current{};
    target_->GetTransform(&current);
    const auto screen = D2D1::Matrix3x2F::ReinterpretBaseType(&current)[0];
    for (int i = 0; i < 3; ++i) {
        brush(gray(0, (0.16f - i * 0.04f) * alpha));
        target_->SetTransform(D2D1::Matrix3x2F::Translation(0, 3.0f + i * 2.5f) * screen);
        target_->FillGeometry(shape.Get(), solid_.Get());
    }
    target_->SetTransform(current);
    const Glass glass{shape, glass_level_ > 0.01f && frostCapture(shape.Get(), glassBlur(20))};
    glassFill(glass, D2D1::ColorF(0.02f, 0.022f, 0.03f, glass.frosted ? 0.86f : 0.94f), alpha);
    brush(gray(1, 0.16f * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
    if (typing) {
        for (int i = 0; i < 3; ++i) {
            const float lift = std::max(0.0f, std::sin(elapsed_ * 7 - i * 0.9f)) * 4;
            brush(gray(0.92f, alpha * (0.55f + 0.45f * lift / 4)));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + padding_x + 9 + i * 18.0f, (rect.top + rect.bottom) * 0.5f - lift), 4.2f, 4.2f), solid_.Get());
        }
    } else drawCompanionText(text, value, mentions, rect.left + padding_x, rect.top + padding_y, gray(0.97f, alpha),
                             D2D1::RectF(0, 0, viewport_.width, viewport_.height), context_->companion->look);
    return rect;
}

void Renderer::drawWidgetPill(D2D1_RECT_F rect, float alpha, float hover) {
    const float radius = (rect.bottom - rect.top) * 0.5f;
    const auto glass = glassPanel(D2D1::RoundedRect(rect, radius, radius), 16);
    if (!glass.shape) return;
    glassFill(glass, D2D1::ColorF(0.04f, 0.05f, 0.07f, glass_level_ > 0.01f ? 0.42f : 0.94f), alpha);
    brush(gray(1, (0.035f + hover * 0.075f) * alpha)); target_->FillGeometry(glass.shape.Get(), solid_.Get());
    brush(gray(1, (0.13f + hover * 0.42f) * alpha)); target_->DrawGeometry(glass.shape.Get(), solid_.Get(), 1.0f);
}

void Renderer::drawCompanionFace(companion::Look look, const avatar::Pose& pose, float cx, float cy, float radius, float alpha, int costume) {
    if (alpha <= 0.003f || radius <= 0) return;
    ComPtr<ID2D1EllipseGeometry> circle;
    if (FAILED(factory_->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), circle.GetAddressOf()))) return;
    // The settings cards' glow (drawCompanionCardGlow) on a dark disc: her colour strongest behind her face.
    brush(gray(0, 0.32f * alpha));
    target_->FillGeometry(circle.Get(), solid_.Get());
    const auto tint = companion_ui::tint(look);
    const D2D1_GRADIENT_STOP stops[] = {
        {0, D2D1::ColorF(tint.r, tint.g, tint.b, 0.62f * alpha)},
        {0.5f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.3f * alpha)},
        {1, D2D1::ColorF(tint.r, tint.g, tint.b, 0.06f * alpha)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1RadialGradientBrush> glow;
    if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) &&
        SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy - radius * 0.08f), {}, radius * 1.1f, radius * 1.1f),
                                                    collection.Get(), glow.GetAddressOf())))
        target_->FillGeometry(circle.Get(), glow.Get());
    // Her face in the middle: where it is on the picture shown (gliding with the cross-fade to the next feeling), at
    // the same size whatever the picture's framing. Without faces.txt: where the square bust usually has her face.
    auto face = companion_ui::faceOf(look, costume, pose.emotion);
    if (const auto before = companion_ui::faceOf(look, costume, pose.previous);
        pose.previous != pose.emotion && pose.blend < 1 && face.size > 0 && before.size > 0) {
        const float t = std::clamp(pose.blend, 0.0f, 1.0f), k = t * t * (3 - 2 * t);
        face = {std::lerp(before.x, face.x, k), std::lerp(before.y, face.y, k), std::lerp(before.size, face.size, k)};
    }
    float x = cx, height = radius * 4.0f, bottom = cy + height * 0.64f;
    if (face.size > 0) {
        const float scale = 2 * radius * companion_ui::face_fill / face.size; // DIPs per canvas pixel.
        height = companion_ui::sprite_height * scale;
        x = cx - (face.x - companion_ui::sprite_width * 0.5f) * scale;
        bottom = cy + (companion_ui::sprite_height - face.y) * scale;
    }
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), circle.Get()), nullptr);
    drawCompanionFigure(look, pose, x, bottom, height, alpha, costume);
    target_->PopLayer();
    brush(gray(1, 0.12f * alpha));
    target_->DrawGeometry(circle.Get(), solid_.Get(), 1);
}

// Flix: a slim pill — her face in a circle, her name and her key.
void Renderer::drawCompanionPill(D2D1_RECT_F rect, float alpha, float hover) {
    const auto& view = *context_->companion;
    if (context_->flix_icons) {
        // Flix with the menu's icons at the top: only her face, as one more small icon (her name on hover).
        const float shown = alpha * context_->flix_icons_alpha;
        if (shown <= 0.003f) return;
        const float cx = (rect.left + rect.right) * 0.5f, cy = (rect.top + rect.bottom) * 0.5f;
        drawCompanionFace(view.look, view.pose, cx, cy, (rect.bottom - rect.top) * 0.5f, shown, view.costume);
        if (hover > 0.004f) {
            brush(gray(1, 0.55f * hover * shown));
            target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), (rect.bottom - rect.top) * 0.5f + 1, (rect.bottom - rect.top) * 0.5f + 1), solid_.Get(), 1.5f);
        }
        if (hover > 0.5f) textLine(view.name, cx - 80, rect.bottom + 16, 160, friend_status_font_.Get(), gray(0.92f, shown), DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    drawWidgetPill(rect, alpha, hover);
    const float cy = (rect.top + rect.bottom) * 0.5f, face = (rect.bottom - rect.top) * 0.5f - 4;
    const float cx = rect.left + 4 + face;
    drawCompanionFace(view.look, view.pose, cx, cy, face, alpha, view.costume);
    const float key_x = rect.right - 4 - 15;
    brush(gray(0.98f, alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(key_x, cy), 15, 15), solid_.Get());
    textLine(view.key, key_x - 15, cy, 30, toast_sub_font_.Get(), gray(0.08f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    textLine(view.name, cx + face + 10, cy, key_x - 15 - 8 - (cx + face + 10), settings_body_font_.Get(), gray(0.98f, alpha));
}

float Renderer::drawCompanionInTile(ID2D1Geometry* shape, float x, float bottom, float figure_height, float alpha) {
    const auto& view = *context_->companion;
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape), nullptr);
    // On start she jumps up out of the tile's bottom edge, as on the default home.
    const float rise = std::max(companion_ui::entranceDrop(view.entrance) * figure_height * 1.1f, 0.0f);
    drawCompanionFigure(view.look, view.pose, x, bottom + rise, figure_height, alpha, view.costume);
    target_->PopLayer();
    return bottom - figure_height * 0.44f;
}

void Renderer::drawCompanionTileBubble(D2D1_RECT_F tile, float mouth_y, float alpha, bool prefer_left) {
    const auto& view = *context_->companion;
    if (view.bubble_alpha <= 0.003f || (!view.bubble_waiting && view.bubble.empty())) return;
    constexpr float width = 380, gap = 10;
    const bool room_left = tile.left - gap - width >= 16, room_right = tile.right + gap + width <= viewport_.width - 16;
    const bool left = prefer_left ? room_left || !room_right : !room_right && room_left;
    const auto bubble = drawSpeechBubble(view.bubble, view.bubble_waiting && view.bubble.empty(), left ? tile.left - gap : tile.right + gap,
                                         mouth_y - 34, width, left ? tile.left + 18 : tile.right - 18, mouth_y,
                                         alpha * view.bubble_alpha, glance_ui::shortcut_top, left);
    companion_bubble_ = {bubble.left, bubble.top, bubble.right, bubble.bottom};
}

void Renderer::drawCompanionTile() {
    const auto& view = *context_->companion;
    const float alpha = view.tile_alpha;
    if (alpha <= 0.003f) return;
    const float hover = view.tile_hover;
    if (view.pill) { drawCompanionPill(box(view.spot), alpha, hover); return; }
    auto r = view.spot;
    r.left -= hover * 3; r.right += hover * 3; r.bottom += hover * 3;
    const auto rect = box(r);
    const float previous_corner = squircle_corner_, previous_reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 80;
    const auto shape = tileShape(rect, tile_shape_);
    squircle_corner_ = previous_corner; corner_reference_ = previous_reference;
    if (!shape) return;
    // The same smoked glass as the Glance tile above it.
    const Glass glass{shape, glass_level_ > 0.01f && frostCapture(shape.Get(), glassBlur(24))};
    glassFill(glass, D2D1::ColorF(0.04f, 0.05f, 0.07f, glass_level_ > 0.01f ? 0.30f : 0.94f), alpha);
    brush(gray(1, (0.035f + hover * 0.075f) * alpha)); target_->FillGeometry(shape.Get(), solid_.Get());
    brush(gray(1, (0.13f + hover * 0.42f) * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
    // Her picture fills the tile and her head rises above it: clipped by the tile's bottom only.
    const auto figure = companion_ui::tileFigure(view.spot);
    ComPtr<ID2D1RectangleGeometry> above;
    ComPtr<ID2D1PathGeometry> mask;
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(factory_->CreateRectangleGeometry(D2D1::RectF(r.left + 1, r.top - 320, r.right - 1, r.bottom - 40), above.GetAddressOf())) &&
        SUCCEEDED(factory_->CreatePathGeometry(mask.GetAddressOf())) && SUCCEEDED(mask->Open(sink.GetAddressOf())) &&
        SUCCEEDED(shape->CombineWithGeometry(above.Get(), D2D1_COMBINE_MODE_UNION, nullptr, sink.Get())) && SUCCEEDED(sink->Close())) {
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.Get()), nullptr);
        // Below the tile's clipped edge at first; never lifted so far that her cut bottom edge shows.
        const float rise = std::max(companion_ui::entranceDrop(view.entrance) * figure.height * 1.1f, 2 - companion_ui::figure_sink);
        drawCompanionFigure(view.look, view.pose, figure.x + hover * 2, figure.bottom + rise, figure.height, alpha, view.costume);
        target_->PopLayer();
    }
    // Her nameplate over the picture: the shortcut, her name and personality.
    const float plate_h = 40, plate_top = r.bottom - 14 - plate_h;
    const float plate_w = std::min(r.right - r.left - 28, 64 + textAdvance(view.name + L"  " + view.persona, settings_body_font_.Get()));
    const auto plate = D2D1::RectF(r.left + 14, plate_top, r.left + 14 + plate_w, plate_top + plate_h);
    brush(D2D1::ColorF(0.02f, 0.022f, 0.03f, 0.72f * alpha)); target_->FillRoundedRectangle(pill(plate), solid_.Get());
    const float cx = plate.left + plate_h * 0.5f, cy = (plate.top + plate.bottom) * 0.5f;
    brush(gray(0.98f, alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 15, 15), solid_.Get());
    textLine(view.key, cx - 15, cy, 30, toast_sub_font_.Get(), gray(0.08f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    textLine(view.name, cx + 24, cy, plate.right - cx - 30, settings_body_font_.Get(), gray(0.98f, alpha));
    const float name_w = textAdvance(view.name + L"  ", settings_body_font_.Get());
    textLine(view.persona, cx + 24 + name_w, cy, plate.right - cx - 30 - name_w, settings_body_font_.Get(), gray(0.68f, alpha));
    // Gamification: her level beside the nameplate, or above it when the tile is too narrow.
    if (view.affinity) {
        const float badge_w = drawCompanionAffinity(0, 0, alpha, false);
        if (plate.right + 8 + badge_w <= r.right - 14) drawCompanionAffinity(plate.right + 8, cy, alpha);
        else drawCompanionAffinity(plate.left, plate.top - 22, alpha);
    }
    companion_bubble_ = {};
    if (view.bubble_alpha > 0.003f && (view.bubble_waiting || !view.bubble.empty())) {
        // The tail points at her mouth from the bubble on her right (screen left).
        const float mouth_y = figure.bottom - figure.height * 0.44f;
        const float tip_x = r.left + 18, tip_y = mouth_y;
        const auto bubble = drawSpeechBubble(view.bubble, view.bubble_waiting && view.bubble.empty(), r.left - 10, mouth_y - 34,
                                             380, tip_x, tip_y, alpha * view.bubble_alpha, glance_ui::shortcut_top);
        companion_bubble_ = {bubble.left, bubble.top, bubble.right, bubble.bottom};
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
    const float enter = smooth(chat.age / 0.45f);
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
    const float duo = view->guest ? smooth(view->duo) : 0.0f;
    const auto pair = layout.pair(duo);
    if (view->guest && view->duo > 0.003f) {
        auto guest = layout.pair(1).guest;
        const float t = std::clamp(view->duo, 0.0f, 1.0f);
        float alpha = std::min(1.0f, t * 2.0f);
        if (view->guest_leaving) guest.x -= (1 - t) * guest.height * 0.9f; // She runs off to the side.
        else guest.bottom += (1 - smooth(t)) * guest.height * 0.45f;       // She pops up from below.
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
        const float slide = (1 - smooth(chat.pose_age / companion_ui::pose_in)) * 60 - smooth((chat.pose_age - companion_ui::pose_hold) / companion_ui::pose_out) * 30;
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
    if (const float shown = smooth(std::clamp(chat.viewer_motion, 0.0f, 1.0f)); shown > 0.003f && view->photo) {
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
    if (const float shown = smooth(std::clamp(chat.mind_motion, 0.0f, 1.0f)); shown > 0.003f)
        drawCompanionMind(layout, chat, shown);
}

namespace {
D2D1_COLOR_F affinityPink(float alpha) { return D2D1::ColorF(1.0f, 0.47f, 0.64f, alpha); }
}

void Renderer::drawCompanionCardGlow(D2D1_RECT_F rect, companion::Look look, float alpha) {
    const auto tint = companion_ui::tint(look);
    const float w = rect.right - rect.left, h = rect.bottom - rect.top;
    const D2D1_GRADIENT_STOP stops[] = {
        {0, D2D1::ColorF(tint.r, tint.g, tint.b, 0.42f * alpha)},
        {0.48f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.18f * alpha)},
        {1, D2D1::ColorF(tint.r, tint.g, tint.b, 0)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1RadialGradientBrush> glow;
    if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) &&
        SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(rect.left + w * 0.5f, rect.top + h * 0.48f), {}, w * 0.62f, h * 0.58f), collection.Get(), glow.GetAddressOf())))
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 12, 12), glow.Get());
}

// A companion card: the interface's tile shape with the compact corners of the frame around it.
ComPtr<ID2D1Geometry> Renderer::companionCardShape(D2D1_RECT_F rect) {
    const float corner = squircle_corner_, reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 64;
    auto shape = tileShape(rect, tile_shape_);
    squircle_corner_ = corner; corner_reference_ = reference;
    return shape;
}

void Renderer::drawCompanionCardFocus(D2D1_RECT_F rect, int surface, float alpha) {
    auto& frame = companion_card_focus_[static_cast<size_t>(surface)];
    auto& previous = companion_card_frame_[static_cast<size_t>(surface)];
    const float dt = elapsed_ - previous;
    if (previous < 0 || dt < 0 || dt > 0.2f || screen_changed_)
        frame.snap(rect.left, rect.top, rect.right, rect.bottom);
    else frame.step(rect.left, rect.top, rect.right, rect.bottom, std::min(dt, 0.05f), spring::glide);
    previous = elapsed_;
    // Around the card's own shape (companionCardShape), its corners grown by the same distance so
    // the two stay concentric; the focus gap moves it further out.
    const float out = 3 + focusOutset(1.8f);
    const auto current = D2D1::RectF(frame.left.value - out, frame.top.value - out, frame.right.value + out, frame.bottom.value + out);
    const float corner = squircle_corner_, reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 64 + out / 0.4f;
    ComPtr<ID2D1Geometry> shape = tileShape(current, tile_shape_);
    squircle_corner_ = corner; corner_reference_ = reference;
    if (surface == 1) {
        // The desktop window's cards keep its own 10 px corners: the frame follows them.
        ComPtr<ID2D1RoundedRectangleGeometry> rounded;
        if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(current, 10 + out, 10 + out), rounded.GetAddressOf()))) shape = rounded;
    }
    if (!shape) return;
    // One outline, like a cover, with the traveling highlight on the same path (no second rim).
    brush(uiAccent(0.72f * alpha));
    target_->DrawGeometry(shape.Get(), solid_.Get(), 1.8f);
    drawOrbitOutline(shape.Get(), uiAccent(), alpha, 2.0f);
}

void Renderer::drawCompanionPicker() {
    const auto* view = context_->companion;
    if (!view || !view->picker) return;
    const auto& picker = *view->picker;
    using L = companion_ui::PickerLayout;
    const L layout{viewport_.width, viewport_.height};
    const auto panel = box(layout.panel(picker));
    const auto shape = D2D1::RoundedRect(panel, desktop_style_ ? 14.0f : 22.0f, desktop_style_ ? 14.0f : 22.0f);
    const auto glass = glassPanel(shape);
    brush(gray(0, 0.6f)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    glassFill(glass, D2D1::ColorF(0.035f, 0.038f, 0.05f, 0.97f));
    brush(gray(1, 0.16f)); target_->DrawRoundedRectangle(shape, solid_.Get(), 1);
    textLine(tr(L"Wybierz towarzyszkę"), panel.left + L::padding, panel.top + 37, panel.right - panel.left - 2 * L::padding,
             settings_heading_font_.Get(), gray(0.97f));
    const int first = layout.first(picker), count = static_cast<int>(picker.choices.size());
    for (int i = first; i < std::min(first + L::page_size, count); ++i) {
        const auto look = picker.choices[static_cast<size_t>(i)];
        const auto* character = companion::character(look);
        if (!character) continue;
        const auto r = box(layout.tile(picker, i));
        const bool chosen = look == view->look, focused = !picker.cancel_focus && picker.focus == i, hovered = picker.hover == i;
        const auto tint = companion_ui::tint(look);
        const D2D1_COLOR_F color = D2D1::ColorF(tint.r, tint.g, tint.b);
        // The interface's tile shape, concentric with the gliding focus frame (drawCompanionCardFocus).
        const auto card_shape = companionCardShape(r);
        if (!card_shape) continue;
        brush(gray(1, focused ? 0.12f : hovered ? 0.09f : 0.045f));
        target_->FillGeometry(card_shape.Get(), solid_.Get());
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), card_shape.Get()), nullptr);
        drawCompanionCardGlow(r, look);
        target_->PopLayer();
        const auto thumb = D2D1::RectF(r.left + 4, r.top + 4, r.right - 4, r.bottom - 60);
        target_->PushAxisAlignedClip(thumb, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const int index = static_cast<int>(look);
        const avatar::Pose pose = focused && index >= 0 && index < static_cast<int>(view->previews.size()) ? view->previews[static_cast<size_t>(index)] : avatar::Pose{};
        const int costume = index >= 0 && index < static_cast<int>(view->preview_costumes.size()) ? view->preview_costumes[static_cast<size_t>(index)] : 0;
        drawCompanionFigure(look, pose, (r.left + r.right) * 0.5f, thumb.bottom + 10, 176, 1, costume);
        target_->PopAxisAlignedClip();
        textLine(companion::name(look), r.left + 12, r.bottom - 39, r.right - r.left - 24, settings_body_font_.Get(), color, DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(chosen ? tr(L"Wybrana") : companion::personaName(character->persona), r.left + 12, r.bottom - 17, r.right - r.left - 24,
                 friend_status_font_.Get(), chosen ? color : gray(0.6f), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (!focused) {
            brush(gray(1, hovered ? 0.25f : 0.08f));
            target_->DrawGeometry(card_shape.Get(), solid_.Get(), 1);
        }
    }
    if (!picker.cancel_focus && count) drawCompanionCardFocus(box(layout.tile(picker, picker.focus)), 2);
    const auto cancel = box(layout.button(picker, L::cancel));
    const auto ink = drawButton(cancel, picker.cancel_focus ? 1.0f : 0.0f, 1, false, picker.hover == L::cancel);
    textLine(tr(L"Anuluj"), cancel.left, (cancel.top + cancel.bottom) * 0.5f, cancel.right - cancel.left, settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (count > L::page_size) {
        for (const int button : {L::previous, L::next}) {
            const auto r = box(layout.button(picker, button));
            const bool enabled = button == L::previous ? first > 0 : first + L::page_size < count;
            const auto color = drawButton(r, 0, 1, false, picker.hover == button, enabled);
            textLine(button == L::previous ? L"‹" : L"›", r.left, (r.top + r.bottom) * 0.5f, r.right - r.left,
                     settings_heading_font_.Get(), color, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        textLine(std::to_wstring(first / L::page_size + 1) + L" / " + std::to_wstring((count + L::page_size - 1) / L::page_size),
                 panel.left + L::padding + 134, panel.bottom - 44, 120, friend_status_font_.Get(), gray(0.65f));
    }
}

float Renderer::drawCompanionAffinity(float left, float cy, float alpha, bool draw, int shown_level, float shown_progress) {
    const auto* view = context_->companion;
    const bool own = shown_level <= 0; // The host's badge: her streak and level-up glow too.
    if (own && (!view || !view->affinity)) return 0;
    const int level_value = own ? view->affinity_level : shown_level;
    const float progress = own ? view->affinity_progress : shown_progress;
    const int streak = own ? view->streak : 0;
    const float pulse = own ? smooth(view->affinity_pulse) : 0.0f;
    // Her card for the player (companionBoundary), still live: yellow, then red.
    const int card = own ? view->affinity_card : 0;
    const float card_pulse = own && card ? smooth(view->affinity_card_pulse) : 0.0f;
    const auto cardColor = [&](float a) { return card == 2 ? D2D1::ColorF(0.94f, 0.24f, 0.24f, a) : D2D1::ColorF(1.0f, 0.82f, 0.2f, a); };
    auto* font = friend_status_font_.Get();
    const std::wstring level = L"Lv " + std::to_wstring(level_value);
    // A streak of days in a row (from two): a flame and the count.
    const std::wstring flame = streak >= 2 ? L"  \U0001F525" + std::to_wstring(streak) : L"";
    const float level_w = textAdvance(level, font), flame_w = flame.empty() ? 0.0f : textAdvance(flame, font);
    constexpr float padding = 12, height = 30, card_gap = 8, card_w = 9, card_h = 13;
    const float card_room = card ? card_gap + card_w : 0.0f;
    const float width = padding * 2 + level_w + flame_w + card_room;
    if (!draw || alpha <= 0.003f) return width;
    const auto rect = D2D1::RectF(left, cy - height * 0.5f, left + width, cy + height * 0.5f);
    // A new level: a pink glow swells and fades around the badge; her card: the same in its colour.
    if (pulse > 0.003f) {
        const float grow = (1 - pulse) * 10 + 2;
        brush(affinityPink(0.45f * pulse * alpha));
        target_->FillRoundedRectangle(pill(D2D1::RectF(rect.left - grow, rect.top - grow, rect.right + grow, rect.bottom + grow)), solid_.Get());
    }
    if (card_pulse > 0.003f) {
        const float grow = (1 - card_pulse) * 10 + 2;
        brush(cardColor(0.4f * card_pulse * alpha));
        target_->FillRoundedRectangle(pill(D2D1::RectF(rect.left - grow, rect.top - grow, rect.right + grow, rect.bottom + grow)), solid_.Get());
    }
    brush(D2D1::ColorF(0.02f, 0.022f, 0.03f, 0.62f * alpha)); target_->FillRoundedRectangle(pill(rect), solid_.Get());
    brush(affinityPink((0.42f + 0.5f * pulse) * alpha)); target_->DrawRoundedRectangle(pill(rect), solid_.Get(), 1.0f + pulse);
    const bool bar = progress >= 0;
    const float text_y = bar ? cy - 2 : cy;
    textLine(level, rect.left + padding, text_y, level_w + 4, font, gray(0.96f, alpha));
    if (!flame.empty()) textLine(flame, rect.left + padding + level_w, text_y, flame_w + 4, font, D2D1::ColorF(1.0f, 0.66f, 0.3f, alpha));
    if (card) { // A referee's card, a little tilted, after the level and the streak.
        const float x = rect.left + padding + level_w + flame_w + card_gap + card_w * 0.5f;
        const auto shape = D2D1::RoundedRect(D2D1::RectF(x - card_w * 0.5f, text_y - card_h * 0.5f, x + card_w * 0.5f, text_y + card_h * 0.5f), 1.6f, 1.6f);
        D2D1_MATRIX_3X2_F transform{};
        target_->GetTransform(&transform);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(12, D2D1::Point2F(x, text_y)) * transform);
        brush(cardColor(alpha)); target_->FillRoundedRectangle(shape, solid_.Get());
        brush(gray(1, 0.35f * alpha)); target_->DrawRoundedRectangle(shape, solid_.Get(), 0.8f);
        target_->SetTransform(transform);
    }
    if (!bar) return width;
    // Her sympathy towards the next level: a thin bar along the badge's bottom.
    const float bar_left = rect.left + padding, bar_right = rect.right - padding, bar_y = rect.bottom - 6;
    brush(gray(1, 0.16f * alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bar_left, bar_y - 1.5f, bar_right, bar_y + 1.5f), 1.5f, 1.5f), solid_.Get());
    const float filled = bar_left + (bar_right - bar_left) * std::clamp(progress, 0.0f, 1.0f);
    if (filled > bar_left + 0.5f) {
        brush(affinityPink(alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bar_left, bar_y - 1.5f, filled, bar_y + 1.5f), 1.5f, 1.5f), solid_.Get());
    }
    return width;
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

// Settings → Asystent → "Postać": only the chosen card animates.
void Renderer::drawCompanionPreviews(const settings_ui::Layout& layout, const settings_ui::Row& row, int index, float scroll, bool focused) {
    const auto r = layout.row(index, scroll);
    textGroup(row.title, row.description, r.left + 24, r.top + 22, r.right - r.left - 48, category_font_.Get(), gray(0.93f),
              settings_body_font_.Get(), gray(0.52f), 1, true);
    const auto& list = companion::characters();
    const int count = static_cast<int>(list.size());
    const int first = settings_ui::Layout::avatarFirst(row.choice, count);
    // The slider glides to its new first card, like the tiles of a shelf, instead of jumping.
    {
        const float dt = elapsed_ - companion_strip_time_;
        if (companion_strip_time_ < 0 || dt < 0 || dt > 0.2f || screen_changed_) companion_strip_first_ = static_cast<float>(first);
        else companion_strip_first_ += (first - companion_strip_first_) * (1 - std::exp(-std::min(dt, 0.05f) * 13.0f));
        if (std::abs(companion_strip_first_ - first) < 0.002f) companion_strip_first_ = static_cast<float>(first);
        companion_strip_time_ = elapsed_;
    }
    const float shown_first = companion_strip_first_;
    const auto* view = context_->companion;
    const auto at = [](const std::vector<int>& values, size_t i) { return i < values.size() ? values[i] : 0; };
    const auto band = layout.row(index, scroll);
    target_->PushAxisAlignedClip(box(settings_ui::Rect{band.left + 18, band.top + 70, band.right - 18, band.bottom - 8}), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int option = 0; option < count; ++option) {
        const auto rect = layout.avatarSlide(index, scroll, option, count, shown_first);
        if (rect.right <= rect.left) continue; // Out of view: the slider shows it when it comes near.
        const auto tile = box(rect);
        const auto& character = list[static_cast<size_t>(option)];
        const auto look = static_cast<companion::Look>(option);
        const bool chosen = row.choice == option;
        // The cards take the interface's tile shape (squircle, rounded or square), as their frame does.
        const auto clip = companionCardShape(tile);
        if (!clip) continue;
        brush(gray(1, chosen ? 0.085f : 0.04f)); target_->FillGeometry(clip.Get(), solid_.Get());
        {
            target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
            drawCompanionCardGlow(tile, look, character.soon ? 0.4f : 1.0f);
            const float height = (tile.bottom - tile.top) * 1.02f;
            const float figure_x = tile.right - (tile.right - tile.left) * 0.3f;
            if (character.soon) {
                // No pictures yet: her colour's glow and a question mark where she will stand.
                textLine(L"?", figure_x - 60, tile.bottom - height * 0.45f, 120, category_font_.Get(), D2D1::ColorF(character.r, character.g, character.b, 0.7f),
                         DWRITE_TEXT_ALIGNMENT_CENTER);
            } else {
                const avatar::Pose pose = view && static_cast<size_t>(option) < view->previews.size() ? view->previews[static_cast<size_t>(option)] : avatar::Pose{};
                drawCompanionFigure(look, pose, figure_x, tile.bottom + height * 0.05f, height, 1, view ? at(view->preview_costumes, static_cast<size_t>(option)) : 0);
            }
            // A dark band under her name, so the caption reads over her picture on a narrow card.
            D2D1_GRADIENT_STOP stops[2]{{0, D2D1::ColorF(0, 0, 0, 0)}, {1, D2D1::ColorF(0, 0, 0, 0.72f)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> fade;
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 2, collection.GetAddressOf())) &&
                SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, tile.bottom - 96), D2D1::Point2F(0, tile.bottom)),
                                                             collection.Get(), fade.GetAddressOf())))
                target_->FillRectangle(D2D1::RectF(tile.left, tile.bottom - 96, tile.right, tile.bottom), fade.Get());
            target_->PopLayer();
        }
        // Gamification on: each one's level in the card's corner (who likes the player more?).
        if (const int level = view && !character.soon ? at(view->preview_levels, static_cast<size_t>(option)) : 0; level > 0)
            drawCompanionAffinity(tile.left + 16, tile.top + 28, 1, true, level, -1);
        // Her name and, under it, her fixed character (Tsundere, Kuudere…).
        textGroup(character.soon ? std::wstring(tr(L"Wkrótce")) : character.name, companion::personaName(character.persona), tile.left + 20, tile.bottom - 38,
                  tile.right - tile.left - 40, category_font_.Get(), character.soon ? gray(0.6f) : gray(0.97f), settings_body_font_.Get(), gray(0.78f), 1);
    }
    target_->PopAxisAlignedClip();
    if (count) {
        const auto chosen = layout.avatarSlide(index, scroll, row.choice, count, shown_first);
        if (chosen.right > chosen.left) drawCompanionCardFocus(box(chosen), 0, focused ? 1.0f : 0.75f);
    }
    // More cards than fit: the slider's arrows on the sides that have more.
    if (count > settings_ui::Layout::avatar_visible) {
        const float cy = (band.top + 84 + band.bottom - 20) * 0.5f;
        if (first > 0) textLine(L"\u2039", band.left + 2, cy, 20, category_font_.Get(), gray(0.8f), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (first + settings_ui::Layout::avatar_visible < count) textLine(L"\u203A", band.right - 22, cy, 20, category_font_.Get(), gray(0.8f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
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
