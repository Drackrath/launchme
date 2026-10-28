// launchme-web: the Linux reader window (BrowserWindow, browser_window_linux.cpp). One WebKitGTK 6.0
// view in a GTK4 window with the launcher's toolbar (back, refresh, play/pause for the TV player,
// close), driven by the launcher over descriptor 3 (browser_protocol.hpp). It runs GTK's own main
// loop; the launcher's SDL loop never sees GTK.
//
// Website data (cookies, local storage, IndexedDB, cache) lives in the profile folder the launcher
// passes: <data folder>/browser/{data,cache}, the same place WebView2 keeps its profile on Windows.
#include "i18n/i18n.hpp"
#include "net/companion_links.hpp"
#include "platform/text_encoding.hpp"
#include "ui/browser_protocol.hpp"
#include "ui/browser_window.hpp"

#include <glib-unix.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
using Command = BrowserWindow::Command;
using Status = BrowserWindow::Status;

constexpr char app_id[] = "io.github.uxandai.Launchme";
constexpr int channel = 3;
constexpr char toggle_script[] = "if(typeof launchmeToggle==='function')launchmeToggle();";
constexpr char style[] = R"(
window.launchme-web, window.launchme-web headerbar { background: #111318; color: #f2f3f5; }
window.launchme-web headerbar { min-height: 68px; padding: 0 14px; box-shadow: none; border: none; }
button.launchme { border-radius: 24px; background: #272a31; color: #f2f3f5; min-height: 46px; min-width: 52px;
    padding: 0 20px; font-size: 17px; border: none; box-shadow: none; outline: none; }
button.launchme:hover { background: #42454d; }
button.launchme.muted { color: #a6aebb; }
button.launchme.chosen { background: #f2f3f5; color: #111318; }
.launchme-heading { font-size: 17px; color: #f2f3f5; }
.launchme-address, .launchme-footer { font-size: 15px; color: #a6aebb; }
.launchme-footer { padding: 10px 20px 12px 20px; }
.launchme-message { font-size: 17px; color: #f2f3f5; background: #111318; padding: 40px; }
)";

std::string text(const wchar_t* polish) { return text_encoding::utf8Lenient(tr(polish)); }

struct Helper {
    std::string input;
    GMainLoop* loop = nullptr;
    GtkWindow* window = nullptr;
    WebKitWebView* view = nullptr;
    WebKitNetworkSession* session = nullptr;
    WebKitWebContext* context = nullptr;
    GtkWidget* buttons[4]{}; // Back, refresh, close, play/pause: BrowserWindow's focus indices.
    GtkWidget *heading = nullptr, *address = nullptr, *footer = nullptr, *message = nullptr;
    Status status = Status::starting;
    std::string url, title, error, fixture, substituted;
    std::filesystem::path profile;
    bool loading = true, can_back = false, can_forward = false, video = false, low_memory = false;
    bool substituting = false, closing = false, minimized = false, active = false;
    int focus = 2;
} app;

void send(std::initializer_list<std::string_view> fields) {
    const auto message = browser_protocol::line(fields);
    size_t sent = 0;
    while (sent < message.size()) {
        const auto wrote = ::send(channel, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote < 0 && errno == EAGAIN) { // The descriptor is non-blocking for reading; wait to write.
            fd_set set; FD_ZERO(&set); FD_SET(channel, &set);
            select(channel + 1, nullptr, &set, nullptr, nullptr);
            continue;
        }
        if (wrote <= 0) return;
        sent += static_cast<size_t>(wrote);
    }
}

int buttonCount() { return app.video && app.status == Status::ready ? 4 : 3; }

void refresh() {
    if (!app.window) return;
    for (int i = 0; i < 4; ++i) {
        auto* button = app.buttons[i];
        gtk_widget_set_visible(button, i < buttonCount());
        if (app.focus == i) gtk_widget_add_css_class(button, "chosen"); else gtk_widget_remove_css_class(button, "chosen");
    }
    if (app.can_back) gtk_widget_remove_css_class(app.buttons[0], "muted"); else gtk_widget_add_css_class(app.buttons[0], "muted");
    const auto heading = app.loading ? text(L"Wczytywanie…") : app.title.empty() ? std::string("Glance") : app.title;
    gtk_label_set_text(GTK_LABEL(app.heading), heading.c_str());
    gtk_label_set_text(GTK_LABEL(app.address), app.url.c_str());
    const auto footer = app.error.empty() ? text(L"↑ ↓ / gałka: przewijaj    ← →: przyciski    A: wybierz    B / Esc: zamknij") : app.error;
    gtk_label_set_text(GTK_LABEL(app.footer), footer.c_str());
    const bool notice = app.status == Status::failed || app.status == Status::starting;
    gtk_widget_set_visible(app.message, notice);
    if (notice) gtk_label_set_text(GTK_LABEL(app.message), app.status == Status::starting ? text(L"Uruchamianie przeglądarki…").c_str() : app.error.c_str());
    const auto window_title = app.title.empty() ? std::string("Glance — Launchme") : app.title + " — Launchme";
    gtk_window_set_title(app.window, window_title.c_str());
}

void reportPage() {
    send({"page", app.url, app.title, app.loading ? "1" : "0", app.can_back ? "1" : "0", app.can_forward ? "1" : "0", app.video ? "1" : "0"});
    refresh();
}
void reportError(std::string message) {
    app.error = std::move(message);
    send({"error", app.error});
    refresh();
}
void reportStatus(Status status) {
    app.status = status;
    send({"status", std::to_string(static_cast<int>(status))});
    refresh();
}
void fail(Status status, std::string message) {
    app.loading = false;
    app.error = std::move(message);
    send({"error", app.error});
    reportStatus(status);
    reportPage();
}

bool isVideo(const std::string& url) {
    return !companion::links::playerHtml(text_encoding::wideLenient(url), true).empty();
}
// Pages served from here, never fetched: the TV YouTube player (under https://launchme.invalid/youtube/
// so the embed receives an origin and a referrer) and, for tests, a fixture for https://launchme.test/.
std::string substitute(const std::string& url) {
    const auto html = companion::links::playerHtml(text_encoding::wideLenient(url), i18n::english());
    if (!html.empty()) return text_encoding::utf8Lenient(html);
    if (!app.fixture.empty() && browser_protocol::webHost(url) == "launchme.test") return app.fixture;
    return {};
}
// Navigations the view may make: web pages, and the inline schemes frames use. Not file:, not
// store or custom protocols (steam://, ...), not javascript: from outside.
bool allowedNavigation(std::string_view uri) {
    if (uri.starts_with("about:") || uri.starts_with("data:") || uri.starts_with("blob:")) return true;
    return browser_protocol::supportedUrl(uri);
}

void history() {
    if (!app.view) return;
    const char* uri = webkit_web_view_get_uri(app.view);
    // A page served from here keeps its first address as the view's URI (WebKit's unreachable URL);
    // the history entry follows pushState.
    if (uri && !app.loading && uri == app.substituted)
        if (auto* item = webkit_back_forward_list_get_current_item(webkit_web_view_get_back_forward_list(app.view)))
            if (const char* current = webkit_back_forward_list_item_get_uri(item)) uri = current;
    if (uri && browser_protocol::supportedUrl(uri)) app.url = uri;
    app.can_back = webkit_web_view_can_go_back(app.view);
    app.can_forward = webkit_web_view_can_go_forward(app.view);
}

void navigate(const std::string& destination) {
    if (!browser_protocol::supportedUrl(destination)) { reportError(text(L"Ten rodzaj linku nie jest obsługiwany w przeglądarce.")); return; }
    app.url = destination;
    app.error.clear();
    send({"error", ""});
    app.loading = true;
    app.video = isVideo(app.url);
    app.focus = app.video ? 3 : std::min(app.focus, 2);
    reportPage();
    if (!app.view) return;
    if (const auto html = substitute(app.url); !html.empty()) {
        app.substituted = app.url;
        webkit_web_view_load_alternate_html(app.view, html.c_str(), app.url.c_str(), app.url.c_str());
    } else webkit_web_view_load_uri(app.view, app.url.c_str());
}

void closeWindow(bool report) {
    if (app.closing) return;
    app.closing = true;
    if (report) send({"closed", app.active ? "1" : "0"});
    if (app.view) webkit_web_view_terminate_web_process(app.view);
    if (app.window) gtk_window_destroy(app.window);
    app.window = nullptr;
    app.view = nullptr;
    g_main_loop_quit(app.loop);
}

void scroll(int pixels) {
    if (!app.view || app.status != Status::ready) return;
    // Follow an inner scroll container (Steam uses these), then the document itself.
    const auto script = "(()=>{const d=" + std::to_string(std::clamp(pixels, -1600, 1600)) +
        ";let e=document.elementFromPoint(innerWidth/2,innerHeight/2);while(e&&e!==document.body){if(e.scrollHeight>e.clientHeight+1&&/auto|scroll/.test(getComputedStyle(e).overflowY)&&(d>0?e.scrollTop+e.clientHeight<e.scrollHeight-1:e.scrollTop>0)){e.scrollBy(0,d);return;}e=e.parentElement;}(document.scrollingElement||document.documentElement).scrollBy(0,d);})()";
    webkit_web_view_evaluate_javascript(app.view, script.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

void restart();

void command(Command cmd) {
    if (!app.window) return;
    switch (cmd) {
    case Command::close: closeWindow(true); break;
    case Command::back: if (app.view && app.can_back) webkit_web_view_go_back(app.view); break;
    case Command::forward: if (app.view && app.can_forward) webkit_web_view_go_forward(app.view); break;
    case Command::reload:
        if (app.status != Status::ready) restart();
        else if (!substitute(app.url).empty()) navigate(app.url); // Served from here: load it again.
        else if (app.view) webkit_web_view_reload(app.view);
        break;
    case Command::previous_button:
        if (app.video && app.status == Status::ready && (app.focus == 2 || app.focus == 3)) app.focus = app.focus == 2 ? 3 : 1;
        else app.focus = std::max(app.can_back ? 0 : 1, app.focus - 1);
        refresh(); break;
    case Command::next_button:
        if (app.video && app.status == Status::ready && (app.focus == 1 || app.focus == 3)) app.focus = app.focus == 1 ? 3 : 2;
        else app.focus = std::min(2, app.focus + 1);
        refresh(); break;
    case Command::accept:
        if (app.focus == 3 && app.video && app.status == Status::ready && app.view)
            webkit_web_view_evaluate_javascript(app.view, toggle_script, -1, nullptr, nullptr, nullptr, nullptr, nullptr);
        else command(app.focus == 0 ? Command::back : app.focus == 1 ? Command::reload : Command::close);
        break;
    }
}

// Low memory: minimized (or hidden by the compositor), the page is unmapped, so WebKit marks it
// hidden and throttles its timers, animations and media; shown again it resumes. WebKit has no
// counterpart of WebView2's per-page memory target for the inactive window.
void applyVisibility() {
    if (app.view) gtk_widget_set_visible(GTK_WIDGET(app.view), !(app.low_memory && app.minimized));
}

void scriptFinished(GObject* source, GAsyncResult* result, gpointer data) {
    const auto id = std::to_string(GPOINTER_TO_SIZE(data));
    GError* error = nullptr;
    JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
    if (!value) { if (error) g_error_free(error); send({"script", id, ""}); return; }
    char* json = jsc_value_to_json(value, 0);
    send({"script", id, json ? json : "null"});
    g_free(json);
    g_object_unref(value);
}

struct Capture { std::string id, path; };
void captureFinished(GObject* source, GAsyncResult* result, gpointer data) {
    std::unique_ptr<Capture> capture(static_cast<Capture*>(data));
    GError* error = nullptr;
    GdkTexture* texture = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(source), result, &error);
    if (error) g_error_free(error);
    const bool saved = texture && gdk_texture_save_to_png(texture, capture->path.c_str());
    if (texture) g_object_unref(texture);
    send({"capture", capture->id, saved ? "1" : "0"});
}

// The whole window as GTK draws it (toolbar, page, footer), for tests' reference captures.
void captureWindow(const std::string& id, const std::string& path) {
    auto* widget = GTK_WIDGET(app.window);
    const int width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
    auto* renderer = gtk_native_get_renderer(GTK_NATIVE(widget));
    bool saved = false;
    if (renderer && width > 0 && height > 0) {
        auto* paintable = gtk_widget_paintable_new(widget);
        auto* snapshot = gtk_snapshot_new();
        gdk_paintable_snapshot(paintable, snapshot, width, height);
        if (auto* node = gtk_snapshot_free_to_node(snapshot)) {
            const graphene_rect_t bounds{{0, 0}, {static_cast<float>(width), static_cast<float>(height)}};
            if (auto* texture = gsk_renderer_render_texture(renderer, node, &bounds)) {
                saved = gdk_texture_save_to_png(texture, path.c_str());
                g_object_unref(texture);
            }
            gsk_render_node_unref(node);
        }
        g_object_unref(paintable);
    }
    send({"capture", id, saved ? "1" : "0"});
}

void handle(const std::vector<std::string>& f) {
    const auto& kind = f[0];
    if (kind == "navigate" && f.size() >= 2) {
        if (app.status == Status::failed) { app.url = f[1]; restart(); }
        else navigate(f[1]);
    } else if (kind == "command" && f.size() >= 2) {
        const int value = std::atoi(f[1].c_str());
        if (value >= 0 && value <= static_cast<int>(Command::accept)) command(static_cast<Command>(value));
    } else if (kind == "scroll" && f.size() >= 2) {
        scroll(std::atoi(f[1].c_str()));
    } else if (kind == "low-memory" && f.size() >= 2) {
        app.low_memory = f[1] == "1";
        applyVisibility();
    } else if (kind == "present") {
        if (app.window) gtk_window_present(app.window);
    } else if (kind == "script" && f.size() >= 3) {
        const auto id = std::strtoull(f[1].c_str(), nullptr, 10);
        if (!app.view) { send({"script", f[1], ""}); return; }
        webkit_web_view_evaluate_javascript(app.view, f[2].c_str(), -1, nullptr, nullptr, nullptr, scriptFinished, GSIZE_TO_POINTER(id));
    } else if (kind == "capture" && f.size() >= 3) {
        if (!app.view) { send({"capture", f[1], "0"}); return; }
        if (f.size() >= 4 && f[3] == "window") { captureWindow(f[1], f[2]); return; }
        webkit_web_view_get_snapshot(app.view, WEBKIT_SNAPSHOT_REGION_VISIBLE, WEBKIT_SNAPSHOT_OPTIONS_NONE, nullptr,
            captureFinished, new Capture{f[1], f[2]});
    } else if (kind == "close") {
        closeWindow(false);
    }
}

gboolean onInput(gint fd, GIOCondition, gpointer) {
    char chunk[16384];
    while (true) {
        const auto got = ::recv(fd, chunk, sizeof(chunk), 0);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 && errno == EAGAIN) break;
        if (got <= 0) { closeWindow(false); return G_SOURCE_REMOVE; } // The launcher is gone.
        app.input.append(chunk, static_cast<size_t>(got));
    }
    for (const auto& line : browser_protocol::takeLines(app.input)) {
        const auto f = browser_protocol::fields(line);
        if (!f.empty()) handle(f);
        if (app.closing) return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

// --- WebKit signals -------------------------------------------------------------------------------

gboolean decidePolicy(WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer) {
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION || type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        auto* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        const char* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
        if (!uri || !allowedNavigation(uri)) {
            webkit_policy_decision_ignore(decision);
            reportError(text(L"Ten rodzaj linku nie jest obsługiwany w przeglądarce."));
            return TRUE;
        }
        return FALSE;
    }
    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        auto* response = WEBKIT_RESPONSE_POLICY_DECISION(decision);
        if (webkit_response_policy_decision_is_mime_type_supported(response)) return FALSE;
        webkit_policy_decision_ignore(decision); // Something to download, not to show.
        if (webkit_response_policy_decision_is_main_frame_main_resource(response))
            reportError(text(L"To okno służy do czytania stron; pobieranie plików jest wyłączone."));
        return TRUE;
    }
    return FALSE;
}

GtkWidget* createWindow(WebKitWebView*, WebKitNavigationAction* action, gpointer) {
    // Pages asking for a new window open in this one, when the user asked for it.
    const char* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
    if (uri && webkit_navigation_action_is_user_gesture(action) && browser_protocol::supportedUrl(uri)) {
        auto* target = g_strdup(uri);
        g_idle_add_once([](gpointer data) { navigate(static_cast<char*>(data)); g_free(data); }, target);
    }
    return nullptr;
}

void loadChanged(WebKitWebView*, WebKitLoadEvent event, gpointer) {
    history();
    if (event == WEBKIT_LOAD_STARTED) {
        app.loading = true;
        if (!app.substituting) { app.error.clear(); send({"error", ""}); }
        app.video = isVideo(app.url);
        app.focus = app.video ? 3 : std::min(app.focus, 2);
    } else if (event == WEBKIT_LOAD_COMMITTED) {
        app.substituting = false;
    } else if (event == WEBKIT_LOAD_FINISHED) {
        if (!app.substituting) app.loading = false; // A failed load whose page is served from here still loads.
    }
    reportPage();
}

gboolean loadFailed(WebKitWebView* view, WebKitLoadEvent, char* failing_uri, GError* error, gpointer) {
    const std::string uri = failing_uri ? failing_uri : "";
    // Links, history and reloads of a page served from here reach the network and fail (.invalid
    // and .test never resolve); the page then takes its place in the history entry.
    if (const auto html = substitute(uri); !html.empty() && !(app.substituting && app.substituted == uri)) {
        app.substituting = true;
        app.substituted = uri;
        webkit_web_view_load_alternate_html(view, html.c_str(), uri.c_str(), uri.c_str());
        return TRUE;
    }
    app.substituting = false;
    if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED) ||
        g_error_matches(error, WEBKIT_POLICY_ERROR, WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE)) return FALSE;
    reportError(text(L"Nie udało się wczytać strony. Użyj Odśwież."));
    return FALSE; // WebKit's own error page, as WebView2 shows its own.
}

void webProcessTerminated(WebKitWebView*, WebKitWebProcessTerminationReason, gpointer) {
    if (!app.closing) fail(Status::failed, text(L"Przeglądarka została zatrzymana. Użyj Odśwież."));
}

gboolean permissionRequest(WebKitWebView*, WebKitPermissionRequest* request, gpointer) {
    webkit_permission_request_deny(request);
    return TRUE;
}

void downloadStarted(WebKitNetworkSession*, WebKitDownload* download, gpointer) {
    webkit_download_cancel(download);
    reportError(text(L"To okno służy do czytania stron; pobieranie plików jest wyłączone."));
}

// --- Window -----------------------------------------------------------------------------------------

gboolean keyPressed(GtkEventControllerKey*, guint key, guint, GdkModifierType state, gpointer) {
    if (key == GDK_KEY_Escape) { command(Command::close); return TRUE; }
    if (key == GDK_KEY_F5) { command(Command::reload); return TRUE; }
    if ((key == GDK_KEY_Left || key == GDK_KEY_KP_Left) && (state & GDK_ALT_MASK)) { command(Command::back); return TRUE; }
    return FALSE;
}

void surfaceState(GObject* surface, GParamSpec*, gpointer) {
    const auto state = gdk_toplevel_get_state(GDK_TOPLEVEL(surface));
    app.minimized = (state & (GDK_TOPLEVEL_STATE_MINIMIZED | GDK_TOPLEVEL_STATE_SUSPENDED)) != 0;
    applyVisibility();
}

GtkWidget* toolbarButton(int index) {
    auto* button = gtk_button_new();
    gtk_widget_add_css_class(button, "launchme");
    gtk_widget_set_focusable(button, FALSE); // The page keeps the keyboard; the gamepad moves `focus`.
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    const auto label = index == 0 ? std::string("‹") : index == 1 ? text(L"Odśwież") : index == 2 ? text(L"Zamknij · Esc / B") : text(L"Odtwórz / Pauza");
    gtk_button_set_label(GTK_BUTTON(button), label.c_str());
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        app.focus = GPOINTER_TO_INT(data);
        command(Command::accept);
    }), GINT_TO_POINTER(index));
    return button;
}

GtkWidget* label(const char* css, float xalign) {
    auto* widget = gtk_label_new("");
    gtk_widget_add_css_class(widget, css);
    gtk_label_set_xalign(GTK_LABEL(widget), xalign);
    gtk_label_set_ellipsize(GTK_LABEL(widget), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(widget), TRUE);
    return widget;
}

bool createView() {
    std::error_code ec;
    const auto data = app.profile / "data", cache = app.profile / "cache";
    std::filesystem::create_directories(data, ec);
    if (!ec) std::filesystem::create_directories(cache, ec);
    if (ec) { fail(Status::failed, text(L"Nie można utworzyć profilu przeglądarki.")); return false; }
    app.session = webkit_network_session_new(data.c_str(), cache.c_str());
    auto* cookies = webkit_network_session_get_cookie_manager(app.session);
    webkit_cookie_manager_set_persistent_storage(cookies, (data / "cookies.sqlite").c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    g_signal_connect(app.session, "download-started", G_CALLBACK(downloadStarted), nullptr);

    if (app.low_memory) {
        auto* pressure = webkit_memory_pressure_settings_new();
        webkit_memory_pressure_settings_set_memory_limit(pressure, 512);
        app.context = WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT, "memory-pressure-settings", pressure, nullptr));
        webkit_memory_pressure_settings_free(pressure);
        webkit_web_context_set_cache_model(app.context, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    } else app.context = webkit_web_context_new();

    auto* settings = webkit_settings_new();
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);
    webkit_settings_set_enable_back_forward_navigation_gestures(settings, TRUE);
    app.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", app.session, "web-context", app.context,
        "settings", settings, nullptr));
    g_object_unref(settings);
    const GdkRGBA background{17 / 255.0f, 19 / 255.0f, 24 / 255.0f, 1};
    webkit_web_view_set_background_color(app.view, &background);
    g_signal_connect(app.view, "decide-policy", G_CALLBACK(decidePolicy), nullptr);
    g_signal_connect(app.view, "create", G_CALLBACK(createWindow), nullptr);
    g_signal_connect(app.view, "load-changed", G_CALLBACK(loadChanged), nullptr);
    g_signal_connect(app.view, "load-failed", G_CALLBACK(loadFailed), nullptr);
    g_signal_connect(app.view, "web-process-terminated", G_CALLBACK(webProcessTerminated), nullptr);
    g_signal_connect(app.view, "permission-request", G_CALLBACK(permissionRequest), nullptr);
    g_signal_connect(app.view, "context-menu", G_CALLBACK(+[](WebKitWebView*, WebKitContextMenu*, WebKitHitTestResult*, gpointer) -> gboolean { return TRUE; }), nullptr);
    g_signal_connect(app.view, "script-dialog", G_CALLBACK(+[](WebKitWebView*, WebKitScriptDialog*, gpointer) -> gboolean { return TRUE; }), nullptr);
    g_signal_connect(app.view, "notify::title", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer) {
        const char* title = webkit_web_view_get_title(app.view);
        app.title = title ? title : "";
        reportPage();
    }), nullptr);
    g_signal_connect(app.view, "notify::uri", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer) { history(); reportPage(); }), nullptr);
    g_signal_connect(webkit_web_view_get_back_forward_list(app.view), "changed",
        G_CALLBACK(+[](WebKitBackForwardList*, WebKitBackForwardListItem*, gpointer, gpointer) { history(); reportPage(); }), nullptr);
    gtk_widget_set_hexpand(GTK_WIDGET(app.view), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(app.view), TRUE);
    return true;
}

void restart() {
    if (!app.view) return;
    app.error.clear();
    reportStatus(Status::ready);
    navigate(app.url);
}

void buildWindow(bool activate) {
    auto* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, style);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    // Dark widgets and prefers-color-scheme: dark for pages (gtk-interface-color-scheme from GTK 4.20).
    auto* gtk_settings = gtk_settings_get_default();
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(gtk_settings), "gtk-interface-color-scheme"))
        g_object_set(gtk_settings, "gtk-interface-color-scheme", 2 /* GTK_INTERFACE_COLOR_SCHEME_DARK */, nullptr);
    else g_object_set(gtk_settings, "gtk-application-prefer-dark-theme", TRUE, nullptr);

    app.window = GTK_WINDOW(gtk_window_new());
    gtk_widget_add_css_class(GTK_WIDGET(app.window), "launchme-web");
    gtk_window_set_default_size(app.window, 1280, 800);
    gtk_widget_set_size_request(GTK_WIDGET(app.window), 700, 450);
    gtk_window_set_icon_name(app.window, app_id);

    // The toolbar is the title bar (dragged and double-clicked like one), with no extra caption buttons.
    auto* bar = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), FALSE);
    for (int i = 0; i < 4; ++i) app.buttons[i] = toolbarButton(i);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), app.buttons[0]);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), app.buttons[1]);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), app.buttons[3]);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), app.buttons[2]);
    auto* titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(titles, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(titles, TRUE);
    app.heading = label("launchme-heading", 0);
    app.address = label("launchme-address", 0);
    gtk_box_append(GTK_BOX(titles), app.heading);
    gtk_box_append(GTK_BOX(titles), app.address);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(bar), titles);
    gtk_window_set_titlebar(app.window, bar);

    auto* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    auto* overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(overlay, TRUE);
    app.message = gtk_label_new("");
    gtk_widget_add_css_class(app.message, "launchme-message");
    gtk_label_set_wrap(GTK_LABEL(app.message), TRUE);
    gtk_label_set_justify(GTK_LABEL(app.message), GTK_JUSTIFY_CENTER);
    gtk_widget_set_hexpand(app.message, TRUE);
    gtk_widget_set_vexpand(app.message, TRUE);
    if (app.view) gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(app.view));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.message);
    app.footer = label("launchme-footer", 0);
    gtk_box_append(GTK_BOX(content), overlay);
    gtk_box_append(GTK_BOX(content), app.footer);
    gtk_window_set_child(app.window, content);

    auto* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(keyPressed), nullptr);
    gtk_widget_add_controller(GTK_WIDGET(app.window), keys);
    g_signal_connect(app.window, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer) -> gboolean { closeWindow(true); return TRUE; }), nullptr);
    g_signal_connect(app.window, "notify::is-active", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer) {
        app.active = app.window && gtk_window_is_active(app.window);
        send({"active", app.active ? "1" : "0"});
    }), nullptr);
    g_signal_connect(app.window, "realize", G_CALLBACK(+[](GtkWidget* widget, gpointer) {
        g_signal_connect(gtk_native_get_surface(GTK_NATIVE(widget)), "notify::state", G_CALLBACK(surfaceState), nullptr);
    }), nullptr);

    refresh();
    if (activate) gtk_window_present(app.window);
    else gtk_widget_set_visible(GTK_WIDGET(app.window), TRUE);
    if (app.view) gtk_widget_grab_focus(GTK_WIDGET(app.view));
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
}

int main(int argc, char** argv) {
    std::string url;
    bool activate = true;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        const bool more = i + 1 < argc;
        if (argument == "--url" && more) url = argv[++i];
        else if (argument == "--profile" && more) app.profile = argv[++i];
        else if (argument == "--language" && more) {
            const int language = std::atoi(argv[++i]);
            if (language >= 0 && language <= static_cast<int>(i18n::Language::es)) i18n::setLanguage(static_cast<i18n::Language>(language));
        } else if (argument == "--low-memory") app.low_memory = true;
        else if (argument == "--no-activate") activate = false;
        else if (argument == "--fixture" && more) app.fixture = readFile(argv[++i]);
    }
    if (::fcntl(channel, F_GETFD) < 0 || url.empty() || !app.profile.is_absolute()) {
        std::fprintf(stderr, "launchme-web: started by Launchme's reader window, not by hand\n");
        return 2;
    }
    ::fcntl(channel, F_SETFL, ::fcntl(channel, F_GETFL) | O_NONBLOCK);
    g_set_prgname(app_id); // Wayland app_id / X11 class: the launcher's desktop entry and icon.
    g_set_application_name("Launchme");
    if (!gtk_init_check()) {
        std::fprintf(stderr, "launchme-web: no display for GTK\n");
        send({"status", std::to_string(static_cast<int>(Status::missing_runtime))});
        return 1;
    }
    app.loop = g_main_loop_new(nullptr, FALSE);
    app.url = url;
    reportStatus(Status::starting);
    const bool created = createView();
    buildWindow(activate);
    g_unix_fd_add(channel, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), onInput, nullptr);
    if (created) {
        reportStatus(Status::ready);
        navigate(url);
    }
    g_main_loop_run(app.loop);
    if (app.session) g_object_unref(app.session);
    if (app.context) g_object_unref(app.context);
    g_main_loop_unref(app.loop);
    return 0;
}
