#pragma once
#include "settings_view.hpp"
#include "../model/devices.hpp"
#include <optional>

namespace device_ui {
enum class Kind { controller, volume, mute, bluetooth, endpoint, pair, windows_audio, windows_bluetooth, cancel, connect, disconnect };
struct Row {
    Kind kind;
    std::wstring id, title, subtitle;
    bool checked = false, input = false;
    std::wstring target;
    std::optional<Kind> action; // Controller connection button, in the device's own row.
    const wchar_t* actionLabel() const {
        return action == Kind::connect ? tr(L"Połącz") : action == Kind::disconnect ? tr(L"Rozłącz") : tr(L"Ustawienia");
    }
};
struct State {
    devices::Panel panel = devices::Panel::controllers;
    devices::Snapshot data;
    std::vector<Row> rows;
    int selected = 0;
    bool fresh = true; // Controllers: the first list of pads moves the focus from "Sparuj" to the first pad.
    // Controllers gallery (pads present): tiles are the pads (rows 1..n) followed by "Sparuj
    // kontroler" (row 0), like Settings → Kontrolery.
    bool gallery() const { return panel == devices::Panel::controllers && rows.size() > 1; }
    int tileOf(int row) const { const int n = static_cast<int>(rows.size()) - 1; return row == 0 ? n : row - 1; }
    int rowOf(int tile) const { const int n = static_cast<int>(rows.size()) - 1; return tile >= n ? 0 : tile + 1; }
    float scroll = 0;
    int audio_column = 0;
    int audio_edit_column = -1;
    std::array<float, 2> audio_scroll{};
    std::array<uint64_t, 2> pending_audio{};
    const devices::Controller* selectedController() const {
        if (selected >= 0 && selected < static_cast<int>(rows.size())) {
            const auto& row = rows[selected];
            for (const auto& pad : data.controllers)
                if (row.id == pad.id || row.id == L"connection:" + pad.id ||
                    (!row.target.empty() && row.target == pad.bluetooth_id)) return &pad;
        }
        return data.controllers.empty() ? nullptr : &data.controllers.front();
    }
    bool audioFooter(int index) const { return index >= 0 && index < static_cast<int>(rows.size()) && (rows[index].kind == Kind::bluetooth || rows[index].kind == Kind::windows_audio); }
    // Up/Down order of one column as it is drawn: the mute button at the card's top, the
    // slider under it, then the device list (rows keep the slider first: it is the default focus).
    std::vector<int> audioRows(int column) const {
        std::vector<int> list;
        for (auto kind : {Kind::mute, Kind::volume})
            for (int i = 0; i < static_cast<int>(rows.size()); ++i) if (rows[i].kind == kind && rows[i].input == (column != 0)) list.push_back(i);
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            if (!audioFooter(i) && rows[i].kind != Kind::mute && rows[i].kind != Kind::volume && rows[i].input == (column != 0)) list.push_back(i);
        return list;
    }
    void moveAudio(int amount) {
        if (!amount) return;
        const auto column = audioRows(audio_column);
        if (column.empty()) return;
        if (selected < 0) { if (amount > 0) selected = column.front(); return; }
        if (audioFooter(selected)) {
            if (amount < 0) selected = column.back();
            return;
        }
        const auto found = std::find(column.begin(), column.end(), selected);
        const int next = static_cast<int>(found - column.begin()) + amount;
        if (next < 0) selected = -1;
        else if (next >= static_cast<int>(column.size())) {
            for (int i = 0; i < static_cast<int>(rows.size()); ++i) if (audioFooter(i)) { selected = i; break; }
        } else selected = column[next];
    }
    void switchAudioColumn(int direction) {
        if (!direction) return;
        if (audioFooter(selected)) { selected = direction > 0 ? static_cast<int>(rows.size()) - 1 : static_cast<int>(rows.size()) - 2; return; }
        const auto from = audioRows(audio_column);
        const auto found = std::find(from.begin(), from.end(), selected);
        const int ordinal = found == from.end() ? 0 : static_cast<int>(found - from.begin());
        audio_column = std::clamp(audio_column + (direction > 0 ? 1 : -1), 0, 1);
        const auto to = audioRows(audio_column);
        if (!to.empty()) selected = to[std::min(ordinal, static_cast<int>(to.size()) - 1)];
    }
    void rebuild() {
        const auto old_id = selected >= 0 && selected < static_cast<int>(rows.size()) ? rows[selected].id : L"";
        rows.clear();
        if (panel == devices::Panel::controllers) {
            rows.push_back({Kind::bluetooth, L"bluetooth", tr(L"Sparuj kontroler"), L"Bluetooth"});
            for (const auto& pad : data.controllers) {
                Row row{Kind::controller, pad.bluetooth_id.empty() ? pad.id : L"controller:" + pad.bluetooth_id,
                    pad.name, pad.connection + L" · " + (pad.connected ? devices::batteryLabel(pad) : std::wstring(tr(L"Rozłączono")))};
                if (!pad.bluetooth_id.empty()) { row.action = pad.connected ? Kind::disconnect : Kind::connect; row.target = pad.bluetooth_id; }
                else if (pad.connection == L"Bluetooth LE") row.action = Kind::windows_bluetooth;
                rows.push_back(std::move(row));
            }
        } else if (panel == devices::Panel::audio) {
            for (bool input : {false, true}) {
                const auto& channel = data.audio[input];
                const std::wstring prefix = input ? L"input:" : L"output:";
                rows.push_back({Kind::volume, prefix + L"volume", input ? tr(L"Nagrywanie") : tr(L"Odtwarzanie"), {}, false, input});
                rows.push_back({Kind::mute, prefix + L"mute", channel.muted ? tr(L"Włącz dźwięk") : tr(L"Wycisz"), {}, channel.muted, input});
                for (const auto& endpoint : data.endpoints) if (endpoint.input == input)
                    rows.push_back({Kind::endpoint, endpoint.id, endpoint.name, endpoint.active ? tr(L"Domyślne") : L"", endpoint.active, input});
            }
            rows.push_back({Kind::bluetooth, L"bluetooth", L"Bluetooth"});
            rows.push_back({Kind::windows_audio, L"windows-audio", tr(L"Dźwięk w Windows")});
        } else {
            rows.push_back({data.busy ? Kind::cancel : Kind::windows_bluetooth, L"windows-bt", data.busy ? tr(L"Anuluj parowanie") : tr(L"Bluetooth w Windows")});
            for (const auto& device : data.bluetooth) rows.push_back({Kind::pair, device.id, device.name,
                device.connected ? tr(L"Połączono") : device.paired ? tr(L"Sparowano") : tr(L"Sparuj"), device.paired});
        }
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == old_id; });
        if (panel == devices::Panel::controllers && fresh && rows.size() > 1 && selected == 0) { selected = 1; fresh = false; }
        else if (!old_id.empty() && found != rows.end()) selected = static_cast<int>(found - rows.begin());
        else if (!old_id.empty() && panel == devices::Panel::audio) {
            const auto fallback = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.kind == Kind::volume && row.input == (audio_column != 0); });
            if (fallback != rows.end()) selected = static_cast<int>(fallback - rows.begin());
        }
        selected = std::clamp(selected, -1, static_cast<int>(rows.size()) - 1);
        if (panel == devices::Panel::audio && selected >= 0 && !audioFooter(selected)) audio_column = rows[selected].input ? 1 : 0;
        if (audio_edit_column >= 0 && (panel != devices::Panel::audio || selected < 0 || rows[selected].kind != Kind::volume ||
            rows[selected].input != (audio_edit_column != 0) || !data.audio[audio_edit_column].available)) audio_edit_column = -1;
    }
};
struct AudioLayout {
    float width, height;
    static constexpr float list_top = 414, row_height = 66, row_step = 76, gap = 36;
    float panelWidth() const { return std::min(1280.0f, width - 128); }
    float left() const { return (width - panelWidth()) * 0.5f; }
    float columnWidth() const { return (panelWidth() - gap) * 0.5f; }
    float columnLeft(int column) const { return left() + column * (columnWidth() + gap); }
    float bottom() const { return height - 160; }
    settings_ui::Rect card(int column) const { const float x = columnLeft(column); return {x, 174, x + columnWidth(), 352}; }
    settings_ui::Rect track(int column) const { const auto r = card(column); return {r.left + 26, 297, r.right - 26, 325}; }
    settings_ui::Rect slider(int column) const { const auto r = card(column); return {r.left + 14, 283, r.right - 14, 339}; }
    settings_ui::Rect mute(int column) const { const auto r = card(column); return {r.right - 68, r.top + 21, r.right - 24, r.top + 65}; }
    settings_ui::Rect endpoint(int column, int index, float scroll) const {
        const float x = columnLeft(column), y = list_top + index * row_step - scroll;
        return {x, y, x + columnWidth(), y + row_height};
    }
    settings_ui::Rect footer(bool windows) const {
        const float x = left() + panelWidth() - (windows ? 242 : 442);
        return {x, height - 112, x + (windows ? 242 : 184), height - 64};
    }
    int ordinal(const State& state, int index) const {
        int count = 0;
        for (int i = 0; i < index; ++i) if (state.rows[i].kind == Kind::endpoint && state.rows[i].input == state.rows[index].input) ++count;
        return count;
    }
    float maximum(const State& state, int column) const {
        int count = 0;
        for (const auto& row : state.rows) if (row.kind == Kind::endpoint && row.input == (column != 0)) ++count;
        return std::max(0.0f, count * row_step - (row_step - row_height) - (bottom() - list_top));
    }
    settings_ui::Rect row(const State& state, int index) const {
        const auto& item = state.rows[index]; const int column = item.input ? 1 : 0;
        if (item.kind == Kind::volume) return slider(column);
        if (item.kind == Kind::mute) return mute(column);
        if (state.audioFooter(index)) return footer(item.kind == Kind::windows_audio);
        return endpoint(column, ordinal(state, index), state.audio_scroll[column]);
    }
    void ensure(State& state) const {
        for (int column = 0; column < 2; ++column) state.audio_scroll[column] = std::clamp(state.audio_scroll[column], 0.0f, maximum(state, column));
        if (state.selected < 0 || state.rows[state.selected].kind != Kind::endpoint) return;
        const int column = state.rows[state.selected].input ? 1 : 0;
        const auto r = row(state, state.selected);
        if (r.top < list_top) state.audio_scroll[column] += r.top - list_top;
        if (r.bottom > bottom()) state.audio_scroll[column] += r.bottom - bottom();
        state.audio_scroll[column] = std::clamp(state.audio_scroll[column], 0.0f, maximum(state, column));
    }
    int hit(const State& state, float x, float y) const {
        // The title/device summary is passive; only the slider and mute are controls.
        for (auto kind : {Kind::mute, Kind::volume, Kind::endpoint, Kind::bluetooth, Kind::windows_audio})
            for (int i = 0; i < static_cast<int>(state.rows.size()); ++i) {
                if (state.rows[i].kind != kind || (kind == Kind::endpoint && (y < list_top || y >= bottom()))) continue;
                if (row(state, i).contains(x, y)) return i;
            }
        return -2;
    }
};
struct Layout {
    float width, height;
    devices::Panel panel = devices::Panel::controllers;
    static constexpr float list_top = 184, row_height = 76, row_step = 84;
    float panelWidth() const { return std::min(panel == devices::Panel::bluetooth ? 1040.0f : 1280.0f, width - 128); }
    float left() const { return (width - panelWidth()) * 0.5f; }
    float listWidth() const { return panel == devices::Panel::controllers ? std::floor((panelWidth() - 48) * 0.6f / 8) * 8 : panelWidth(); }
    float bottom() const { return height - 112; }
    settings_ui::Rect back() const { return {left(), 58, left() + 58, 104}; }
    settings_ui::Rect row(int index, float scroll) const {
        const float top = list_top + index * row_step - scroll;
        return {left(), top, left() + listWidth(), top + row_height};
    }
    settings_ui::Rect controllerAction(int index, float scroll) const {
        const auto r = row(index, scroll);
        const float top = (r.top + r.bottom - 44) * 0.5f;
        return {r.right - 148, top, r.right - 16, top + 44};
    }
    settings_ui::Rect preview() const { return {left() + listWidth() + 48, list_top, left() + panelWidth(), list_top + 320}; }
    settings_ui::Rect controllerEmpty() const {
        const float center = width * 0.5f, top = (list_top + bottom()) * 0.5f - 160;
        return {center - 280, top, center + 280, top + 320};
    }
    settings_ui::Rect controllerEmptyButton() const {
        const auto area = controllerEmpty();
        return {width * 0.5f - 112, area.top + 206, width * 0.5f + 112, area.top + 254};
    }
    // Controllers gallery: large tiles, centred while they fit, else sliding with the focused one.
    static constexpr float tile_width = 288, tile_height = 384, tile_gap = 24;
    int visibleTiles() const { return std::max(1, static_cast<int>((panelWidth() + tile_gap) / (tile_width + tile_gap))); }
    int firstTile(int count, int focus) const {
        const int visible = visibleTiles();
        return count <= visible ? 0 : std::clamp(focus - visible / 2, 0, count - visible);
    }
    settings_ui::Rect tile(int which, int count, int focus) const {
        const int shown = std::min(count, visibleTiles());
        const float strip = shown * (tile_width + tile_gap) - tile_gap;
        const float x = left() + (panelWidth() - strip) * 0.5f + (which - firstTile(count, focus)) * (tile_width + tile_gap);
        return {x, list_top, x + tile_width, list_top + tile_height};
    }
    static settings_ui::Rect tileAction(settings_ui::Rect tile) { return {tile.left + 16, tile.bottom - 58, tile.right - 16, tile.bottom - 16}; }
    int hitTile(float x, float y, int count, int focus) const {
        const int first = firstTile(count, focus), end = std::min(count, first + visibleTiles());
        for (int i = first; i < end; ++i) if (tile(i, count, focus).contains(x, y)) return i;
        return -1;
    }
    int hit(float x, float y, size_t count, float scroll) const {
        if (panel == devices::Panel::controllers && count == 1) return controllerEmptyButton().contains(x, y) ? 0 : -2;
        if (y < list_top || y >= bottom()) return -2;
        for (int i = 0; i < static_cast<int>(count); ++i) if (row(i, scroll).contains(x, y)) return i;
        return -2;
    }
    int focusTile(const State& state) const { return state.selected < 0 ? 0 : state.tileOf(state.selected); }
    // The row of the gallery tile under a point, -2 for none.
    int hitGallery(const State& state, float x, float y) const {
        const int which = hitTile(x, y, static_cast<int>(state.rows.size()), focusTile(state));
        return which < 0 ? -2 : state.rowOf(which);
    }
    void ensure(State& state) const {
        if (state.panel == devices::Panel::audio) { AudioLayout{width, height}.ensure(state); return; }
        if (state.gallery()) { state.scroll = 0; return; }
        if (state.panel == devices::Panel::controllers && state.data.controllers.empty()) { state.scroll = 0; return; }
        if (state.selected < 0) return;
        const auto item = row(state.selected, state.scroll);
        if (item.top < list_top) state.scroll += item.top - list_top;
        if (item.bottom > bottom()) state.scroll += item.bottom - bottom();
        state.scroll = std::clamp(state.scroll, 0.0f, std::max(0.0f,
            list_top + state.rows.size() * row_step - (row_step - row_height) - bottom()));
    }
};
}
