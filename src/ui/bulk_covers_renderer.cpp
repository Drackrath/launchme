#include "renderer.hpp"

void Renderer::drawBulkCovers() {
    if (!context_->bulk_covers || !context_->bulk_covers->open) return;
    const auto& ui = *context_->bulk_covers; const auto& state = ui.progress;
    const bulk_covers_ui::Layout layout{viewport_.width, viewport_.height, ui.buttons()}; const auto p = layout.panel();
    const auto rect = [](settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); };
    brush(D2D1::ColorF(0, 0.8f)); target_->FillRectangle(D2D1::RectF(0,0,viewport_.width,viewport_.height),solid_.Get());
    brush(D2D1::ColorF(0.045f,0.055f,0.07f)); target_->FillRoundedRectangle(D2D1::RoundedRect(rect(p),18,18),solid_.Get());
    if (panel_borders_) { brush(D2D1::ColorF(1,1,1,0.18f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(rect(p),18,18),solid_.Get()); }
    const float x = p.left+32, w = p.right-p.left-64;
    textLine(tr(L"Pobierz brakujące okładki"),x,p.top+40,w,category_font_.Get(),D2D1::ColorF(0xffffff));
    textLine(tr(L"Kwadratowe · pionowe · poziome"),x,p.top+74,w,settings_body_font_.Get(),D2D1::ColorF(0xaeb9c0));
    const bool running = state.phase == bulk_covers::Phase::running && !ui.paused && !ui.cancelling;
    textLine(running && !state.current.empty() ? state.current : bulk_covers_ui::status(ui),x,p.top+116,w,menu_font_.Get(),D2D1::ColorF(0xffffff));
    textLine(bulk_covers_ui::summary(ui),x,p.top+150,w,settings_body_font_.Get(),D2D1::ColorF(0xaeb9c0));
    const float ratio = state.total ? std::min(1.0f,static_cast<float>(state.completed())/static_cast<float>(state.total)) : 0;
    brush(D2D1::ColorF(1,1,1,0.12f)); target_->FillRectangle(D2D1::RectF(x,p.top+181,x+w,p.top+189),solid_.Get());
    brush(uiAccent()); target_->FillRectangle(D2D1::RectF(x,p.top+181,x+w*ratio,p.top+189),solid_.Get());
    auto progress = std::to_wstring(state.completed()) + L" / " + std::to_wstring(state.total);
    if (!state.terminal() && !ui.paused) {
        if (state.wait_seconds) progress += std::wstring(L" · ") + tr(L"Ponowienie za: ") + bulk_covers_ui::duration(state.wait_seconds);
        else if (state.eta_seconds) progress += std::wstring(L" · ") + tr(L"Pozostało: ") + bulk_covers_ui::duration(*state.eta_seconds);
    }
    textLine(progress,x,p.top+214,w,settings_body_font_.Get(),D2D1::ColorF(0xaeb9c0));
    for (int i=0;i<ui.buttons();++i) {
        const auto b=layout.button(i);
        const bool enabled = i == 1 || (!ui.cancelling && state.phase != bulk_covers::Phase::scanning);
        const auto ink=drawButton(rect(b),ui.focus==i&&enabled?1.0f:0.0f,1,false,false,enabled);
        textLine(i ? tr(L"Anuluj") : bulk_covers_ui::primary(ui),b.left,(b.top+b.bottom)/2,b.right-b.left,menu_font_.Get(),ink,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}
