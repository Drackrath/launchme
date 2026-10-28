// launchme-browser: the reader window on Linux (the WebView2 window of src/ui/browser_window.cpp on
// Windows). One GTK 4 window with a WebKitGTK 6.0 view and the same toolbar: back, reload, play /
// pause on the TV video player, the page's title and address, close. The launcher drives it over
// the socket it passes as stdout (browser_protocol.hpp) and hears back its state; the helper ends
// with that socket.
//
// launchme-browser --profile <dir> [--title <text>] [--language <bcp47>] [--width <px> --height <px>]
//                  [--maximize] [--no-activate] [--low-memory] [--parent x11:<xid>|wayland:<handle>]
// launchme-browser --check     exits 0 when GTK 4 and WebKitGTK 6.0 load.
#include "browser_protocol.hpp"

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#endif
#ifdef LAUNCHME_BROWSER_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#endif
#include <glib-unix.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
namespace protocol = browser_protocol;

struct Options {
    std::string profile, title = "Glance \u2014 Launchme", language, parent;
    int width = 0, height = 0;
    bool maximize = false, activate = true, low_memory = false;
};

// Toolbar buttons by focus index, as on Windows: 0 back, 1 reload, 2 close, 3 play / pause.
enum ToolbarButton { back_button, reload_button, close_button, play_button, button_count };

struct App {
    Options options;
    int channel = -1;
    std::string input;
    bool done = false, shown = false, closing = false;
    GtkWindow* window = nullptr;
    WebKitWebView* view = nullptr;
    WebKitWebContext* context = nullptr;
    WebKitNetworkSession* session = nullptr;
    GtkWidget* buttons[button_count]{};
    GtkWidget *heading = nullptr, *address = nullptr, *footer = nullptr, *message = nullptr;
    std::map<std::string, std::string> texts;
    std::map<std::string, std::string> pages; // launchme.invalid URL -> document (the launcher's)
    std::string own_load, awaiting_page;      // the alternate document being loaded / requested
    std::string failed_url;                   // WebKit's error page for it keeps the failure shown
    std::string url, title, error, status = "starting";
    bool loading = true, can_back = false, can_forward = false, video = false, fullscreen = false;
    bool active = false, hidden = false, low_memory = false, load_failed = false;
    bool requested = false; // A load we asked for has not started yet: an ending one is not the end.
    bool document_load = false; // Between WebKit's load-changed STARTED and FINISHED.
    int history_steps = 0;      // Back (<0) / forward (>0) asked for while a same-document step settles.
    int focus = close_button, hover = -1;
    SoupServer* fixture_server = nullptr;
    std::string fixture;
};
App app;
const bool debug = std::getenv("LAUNCHME_BROWSER_DEBUG") != nullptr; // Traces to stderr.

std::string text(const char* key, const char* fallback = "") {
    const auto found = app.texts.find(key);
    return found != app.texts.end() ? found->second : fallback;
}

// ---- The launcher's channel -------------------------------------------------------------------

void send(std::initializer_list<std::string_view> fields) {
    if (app.channel < 0) return;
    const auto line = protocol::message(fields);
    size_t sent = 0;
    while (sent < line.size()) {
        const ssize_t written = ::send(app.channel, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);
        if (written < 0 && errno == EINTR) continue;
        if (written < 0 && errno == EAGAIN) {
            pollfd wait{app.channel, POLLOUT, 0};
            if (::poll(&wait, 1, 1000) > 0) continue;
        }
        if (written <= 0) { ::close(app.channel); app.channel = -1; app.done = true; return; }
        sent += static_cast<size_t>(written);
    }
}

bool playerUrl(std::string_view url) { return url.starts_with(protocol::player_origin); }
bool videoUrl(std::string_view url) { return url.starts_with("https://launchme.invalid/youtube/"); }

// ---- The toolbar and the messages around the page ----------------------------------------------

void refresh() {
    if (!app.window) return;
    const bool ready = app.status == "ready";
    const bool video = app.video && ready;
    for (int i = 0; i < button_count; ++i) {
        GtkWidget* button = app.buttons[i];
        if (app.focus == i) gtk_widget_add_css_class(button, "chosen"); else gtk_widget_remove_css_class(button, "chosen");
        if (app.hover == i) gtk_widget_add_css_class(button, "hover"); else gtk_widget_remove_css_class(button, "hover");
    }
    if (app.can_back) gtk_widget_remove_css_class(app.buttons[back_button], "muted");
    else gtk_widget_add_css_class(app.buttons[back_button], "muted");
    gtk_widget_set_visible(app.buttons[play_button], video);
    gtk_button_set_label(GTK_BUTTON(app.buttons[reload_button]), text("reload", "Reload").c_str());
    gtk_button_set_label(GTK_BUTTON(app.buttons[close_button]), text("close", "Close \u00b7 Esc / B").c_str());
    gtk_button_set_label(GTK_BUTTON(app.buttons[play_button]), text("play", "Play / Pause").c_str());
    gtk_label_set_text(GTK_LABEL(app.heading), app.loading ? text("loading", "Loading\u2026").c_str() : app.title.empty() ? "Glance" : app.title.c_str());
    gtk_label_set_text(GTK_LABEL(app.address), app.url.c_str());
    gtk_label_set_text(GTK_LABEL(app.footer), app.error.empty() ? text("hint").c_str() : app.error.c_str());
    const bool message = app.status == "starting" || app.status == "failed";
    gtk_label_set_text(GTK_LABEL(app.message), app.status == "starting" ? text("starting").c_str() : app.error.c_str());
    gtk_widget_set_visible(app.message, message);
}

void setStatus(const char* status) {
    if (app.status == status) return;
    app.status = status;
    send({"status", status});
    refresh();
}

void setError(const std::string& error) {
    if (app.error == error) return;
    app.error = error;
    send({"error", error});
    refresh();
}

void setLoading(bool loading) {
    if (app.loading == loading) return;
    app.loading = loading;
    send({"loading", loading ? "1" : "0"});
    refresh();
}

void setUrl(const std::string& url) {
    if (!protocol::supportedUrl(url)) return;
    const bool video = videoUrl(url);
    if (video != app.video) app.focus = video ? play_button : std::min(app.focus, static_cast<int>(close_button));
    app.video = video;
    if (url == app.url) { refresh(); return; }
    app.url = url;
    send({"url", url});
    refresh();
}

void history() {
    if (!app.view) return;
    const bool back = webkit_web_view_can_go_back(app.view), forward = webkit_web_view_can_go_forward(app.view);
    if (back == app.can_back && forward == app.can_forward) return;
    app.can_back = back; app.can_forward = forward;
    send({"history", back ? "1" : "0", forward ? "1" : "0"});
    refresh();
}

// ---- Navigation ---------------------------------------------------------------------------------

// Back and forward. A same-document step (history.pushState entries) is still settling while
// is-loading is set without a document load; one more step then would be lost, so it waits.
void historyStep(int step) {
    if (!app.view || !step) return;
    if (webkit_web_view_is_loading(app.view) && !app.document_load) { app.history_steps += step; return; }
    if (step < 0 && webkit_web_view_can_go_back(app.view)) webkit_web_view_go_back(app.view);
    else if (step > 0 && webkit_web_view_can_go_forward(app.view)) webkit_web_view_go_forward(app.view);
    else app.history_steps = 0; // Nothing in that direction: the queued steps go too.
}

void requestLoad() {
    app.history_steps = 0;
    app.failed_url.clear();
    app.requested = true;
    setLoading(true);
}

// A launchme.invalid document comes from the launcher and is loaded under its own https URL, so the
// YouTube embed sees a real origin and referrer (WebView2 serves it through WebResourceRequested).
// It is alternate content for that URL: reload, back and forward come through decidePolicy again.
void loadPage(const std::string& url) {
    requestLoad();
    const auto found = app.pages.find(url);
    if (found == app.pages.end()) { app.awaiting_page = url; send({"player", url}); return; }
    if (found->second.empty()) { app.requested = false; setError(text("unsupported")); setLoading(false); return; }
    app.own_load = url;
    setUrl(url);
    webkit_web_view_load_alternate_html(app.view, found->second.c_str(), url.c_str(), url.c_str());
}

void navigate(const std::string& url) {
    if (!protocol::supportedUrl(url)) { setError(text("unsupported")); return; }
    if (app.status == "failed") setStatus("ready"); // A terminated web process is started again.
    requestLoad(); // Before the address, as WebView2's NavigationStarting reports both.
    setUrl(url);
    setError({});
    if (playerUrl(url)) loadPage(url);
    else webkit_web_view_load_uri(app.view, url.c_str());
}

void evaluate(const std::string& script) {
    if (app.view) webkit_web_view_evaluate_javascript(app.view, script.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

void scroll(int pixels) {
    if (!app.view || app.status != "ready") return;
    // Follow an inner scroll container (Steam uses these), then the document itself.
    const auto script = "(()=>{const d=" + std::to_string(std::clamp(pixels, -1600, 1600)) +
        ";let e=document.elementFromPoint(innerWidth/2,innerHeight/2);while(e&&e!==document.body){if(e.scrollHeight>e.clientHeight+1&&"
        "/auto|scroll/.test(getComputedStyle(e).overflowY)&&(d>0?e.scrollTop+e.clientHeight<e.scrollHeight-1:e.scrollTop>0)){e.scrollBy(0,d);return;}"
        "e=e.parentElement;}(document.scrollingElement||document.documentElement).scrollBy(0,d);})()";
    evaluate(script);
}

void reload() {
    if (!app.view) return;
    if (app.status == "failed") setStatus("ready"); // A terminated web process is started again.
    requestLoad();
    setError({});
    if (playerUrl(app.url)) loadPage(app.url);
    else webkit_web_view_reload(app.view);
}

void closeWindow(bool notify) {
    if (app.closing) return;
    app.closing = true;
    if (notify) send({"closed"});
    if (app.window) gtk_widget_set_visible(GTK_WIDGET(app.window), FALSE);
    app.done = true;
}

void accept() {
    if (app.focus == play_button && app.video && app.status == "ready")
        evaluate("if(typeof launchmeToggle==='function')launchmeToggle();");
    else if (app.focus == back_button) historyStep(-1);
    else if (app.focus == reload_button) reload();
    else closeWindow(true);
}

void button(std::string_view which) {
    const bool video = app.video && app.status == "ready";
    if (which == "previous") {
        if (video && (app.focus == close_button || app.focus == play_button)) app.focus = app.focus == close_button ? play_button : reload_button;
        else app.focus = std::max(app.can_back ? 0 : 1, app.focus - 1);
    } else if (which == "next") {
        if (video && (app.focus == reload_button || app.focus == play_button)) app.focus = app.focus == reload_button ? play_button : close_button;
        else app.focus = std::min(static_cast<int>(close_button), app.focus + 1);
    } else if (which == "accept") {
        accept();
        return;
    }
    refresh();
}

// ---- Low memory ---------------------------------------------------------------------------------

// AppSettings::low_memory: no back/forward page cache and the smallest memory caches; minimized
// (or hidden by the compositor) the view is unmapped, so the page is hidden and its timers and
// rendering are throttled until it is shown again (WebView2: MemoryUsageTargetLevel, TrySuspend).
void applyMemory() {
    if (!app.view) return;
    webkit_settings_set_enable_page_cache(webkit_web_view_get_settings(app.view), !app.low_memory);
    webkit_web_context_set_cache_model(app.context, app.low_memory ? WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER : WEBKIT_CACHE_MODEL_WEB_BROWSER);
    gtk_widget_set_visible(GTK_WIDGET(app.view), !(app.low_memory && app.hidden));
}

void surfaceState(GdkSurface* surface) {
    if (!GDK_IS_TOPLEVEL(surface)) return;
    const auto state = gdk_toplevel_get_state(GDK_TOPLEVEL(surface));
    GdkToplevelState hidden = GDK_TOPLEVEL_STATE_MINIMIZED;
#if GTK_CHECK_VERSION(4, 12, 0)
    hidden = static_cast<GdkToplevelState>(hidden | GDK_TOPLEVEL_STATE_SUSPENDED);
#endif
    const bool now_hidden = (state & hidden) != 0;
    if (now_hidden == app.hidden) return;
    app.hidden = now_hidden;
    applyMemory();
}

// ---- Test hooks: snapshots and scripts ---------------------------------------------------------

struct Request { std::string id, path; };

void snapshotDone(GObject* source, GAsyncResult* result, gpointer data) {
    auto* request = static_cast<Request*>(data);
    GError* error = nullptr;
    GdkTexture* texture = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(source), result, &error);
    const bool saved = texture && gdk_texture_save_to_png(texture, request->path.c_str());
    if (texture) g_object_unref(texture);
    if (error) g_error_free(error);
    send({"snapshot", request->id, saved ? "1" : "0"});
    delete request;
}

// The window as drawn (toolbar, page and footer), for checking the layout. Its render node exists
// once a frame has been drawn after the last change, so a pending redraw is waited for.
bool snapshotWindow(const std::string& path) {
    GtkWidget* widget = GTK_WIDGET(app.window);
    const int width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
    GskRenderer* renderer = gtk_native_get_renderer(GTK_NATIVE(app.window));
    if (width <= 0 || height <= 0 || !renderer) return false;
    GdkPaintable* paintable = gtk_widget_paintable_new(widget);
    GtkSnapshot* snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), width, height);
    GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
    bool saved = false;
    if (node) {
        const graphene_rect_t area = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(width), static_cast<float>(height));
        if (GdkTexture* texture = gsk_renderer_render_texture(renderer, node, &area)) {
            saved = gdk_texture_save_to_png(texture, path.c_str());
            g_object_unref(texture);
        }
        gsk_render_node_unref(node);
    }
    g_object_unref(paintable);
    if (debug) g_printerr("launchme-browser: window snapshot %dx%d %s\n", width, height, saved ? "saved" : node ? "not saved" : "not drawn yet");
    return saved;
}

struct WindowSnapshot { std::string id, path; int attempts = 0; };

gboolean retryWindowSnapshot(gpointer data) {
    auto* request = static_cast<WindowSnapshot*>(data);
    if (!app.window || app.done) { delete request; return G_SOURCE_REMOVE; }
    if (snapshotWindow(request->path) || ++request->attempts >= 40) {
        send({"snapshot", request->id, request->attempts >= 40 ? "0" : "1"});
        delete request;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
void scriptDone(GObject* source, GAsyncResult* result, gpointer data) {
    auto* request = static_cast<Request*>(data);
    GError* error = nullptr;
    JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
    if (!value) {
        send({"result", request->id, "0", ""});
    } else {
        char* json = jsc_value_is_undefined(value) ? nullptr : jsc_value_to_json(value, 0);
        send({"result", request->id, "1", json ? json : "null"});
        g_free(json);
        g_object_unref(value);
    }
    if (error) g_error_free(error);
    delete request;
}

// Tests: a loopback proxy answers every http:// request with the fixture and refuses CONNECT, so
// nothing reaches the network (the WebView2 test serves its fixture through WebResourceRequested).
void serveFixture(SoupServer*, SoupServerMessage* message, const char*, GHashTable*, gpointer) {
    if (g_strcmp0(soup_server_message_get_method(message), "GET") != 0 && g_strcmp0(soup_server_message_get_method(message), "HEAD") != 0) {
        soup_server_message_set_status(message, SOUP_STATUS_FORBIDDEN, nullptr);
        return;
    }
    soup_message_headers_append(soup_server_message_get_response_headers(message), "Cache-Control", "no-store");
    soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
    soup_server_message_set_response(message, "text/html; charset=utf-8", SOUP_MEMORY_COPY, app.fixture.data(), app.fixture.size());
}

void startFixture(std::string html) {
    app.fixture = std::move(html);
    if (app.fixture_server) return;
    app.fixture_server = soup_server_new("server-header", "launchme-fixture", nullptr);
    soup_server_add_handler(app.fixture_server, nullptr, serveFixture, nullptr, nullptr);
    GError* error = nullptr;
    if (!soup_server_listen_local(app.fixture_server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error)) {
        g_printerr("launchme-browser: fixture server: %s\n", error ? error->message : "?");
        if (error) g_error_free(error);
        return;
    }
    GSList* uris = soup_server_get_uris(app.fixture_server);
    const int port = uris ? g_uri_get_port(static_cast<GUri*>(uris->data)) : 0;
    g_slist_free_full(uris, reinterpret_cast<GDestroyNotify>(g_uri_unref));
    const auto proxy = "http://127.0.0.1:" + std::to_string(port);
    WebKitNetworkProxySettings* settings = webkit_network_proxy_settings_new(proxy.c_str(), nullptr);
    webkit_network_session_set_proxy_settings(app.session, WEBKIT_NETWORK_PROXY_MODE_CUSTOM, settings);
    webkit_network_proxy_settings_free(settings);
}

// ---- The launcher's messages ------------------------------------------------------------------

void present() {
    if (!app.window) return;
    gtk_window_present(app.window);
}

void receive(const std::vector<std::string>& fields) {
    const auto& verb = fields[0];
    if (debug && app.view) g_printerr("launchme-browser: <- %s (uri %s, back %d, forward %d)\n", verb.c_str(), webkit_web_view_get_uri(app.view),
        webkit_web_view_can_go_back(app.view), webkit_web_view_can_go_forward(app.view));
    const auto field = [&](size_t index) -> const std::string& { static const std::string none; return index < fields.size() ? fields[index] : none; };
    if (verb == "text") { app.texts[field(1)] = field(2); refresh(); }
    else if (verb == "fixture") startFixture(field(1));
    else if (verb == "page") {
        app.pages[field(1)] = field(2);
        if (app.awaiting_page == field(1)) { app.awaiting_page.clear(); loadPage(field(1)); }
    }
    else if (verb == "navigate") navigate(field(1));
    else if (verb == "show") {
        if (!app.shown) { app.shown = true; present(); setStatus("ready"); }
    }
    else if (verb == "raise") present();
    else if (verb == "back") historyStep(-1);
    else if (verb == "forward") historyStep(1);
    else if (verb == "reload") reload();
    else if (verb == "close") closeWindow(false);
    else if (verb == "scroll") scroll(std::atoi(field(1).c_str()));
    else if (verb == "button") button(field(1));
    else if (verb == "low-memory") { app.low_memory = field(1) == "1"; applyMemory(); }
    else if (verb == "script") {
        if (!app.view) { send({"result", field(1), "0", ""}); return; }
        webkit_web_view_evaluate_javascript(app.view, field(2).c_str(), -1, nullptr, nullptr, nullptr, scriptDone, new Request{field(1), {}});
    } else if (verb == "snapshot") {
        if (!app.view) { send({"snapshot", field(1), "0"}); return; }
        if (field(2) == "window") {
            auto* request = new WindowSnapshot{field(1), field(3)};
            if (retryWindowSnapshot(request) == G_SOURCE_CONTINUE) g_timeout_add(50, retryWindowSnapshot, request);
            return;
        }
        webkit_web_view_get_snapshot(app.view, WEBKIT_SNAPSHOT_REGION_VISIBLE, WEBKIT_SNAPSHOT_OPTIONS_NONE, nullptr, snapshotDone, new Request{field(1), field(3)});
    }
}

gboolean channelReady(gint fd, GIOCondition, gpointer) {
    char chunk[65536];
    for (;;) {
        const ssize_t got = ::read(fd, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (got <= 0) { closeWindow(false); return G_SOURCE_REMOVE; } // The launcher is gone.
        app.input.append(chunk, static_cast<size_t>(got));
        size_t start = 0;
        for (size_t end; !app.done && (end = app.input.find('\n', start)) != std::string::npos; start = end + 1)
            receive(protocol::fields(std::string_view(app.input).substr(start, end - start)));
        app.input.erase(0, start);
        if (app.input.size() > protocol::max_line) { closeWindow(false); return G_SOURCE_REMOVE; }
        if (app.done) return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

// ---- WebKit's signals ---------------------------------------------------------------------------

bool allowedScheme(std::string_view url) {
    // Frames and the page's own documents use these; other schemes (mailto:, steam:, ...) are not
    // opened from the reader window (WebView2 cancels them in NavigationStarting).
    for (const std::string_view scheme : {"http:", "https:", "about:", "data:", "blob:", "javascript:"}) {
        if (url.size() < scheme.size()) continue;
        if (g_ascii_strncasecmp(url.data(), scheme.data(), scheme.size()) == 0) return true;
    }
    return false;
}

gboolean decidePolicy(WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer) {
    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        auto* response = WEBKIT_RESPONSE_POLICY_DECISION(decision);
        if (!webkit_response_policy_decision_is_main_frame_main_resource(response)) return FALSE;
        WebKitURIResponse* answer = webkit_response_policy_decision_get_response(response);
        SoupMessageHeaders* headers = webkit_uri_response_get_http_headers(answer);
        const char* disposition = headers ? soup_message_headers_get_one(headers, "Content-Disposition") : nullptr;
        const bool attachment = disposition && g_ascii_strncasecmp(disposition, "attachment", 10) == 0;
        if (webkit_response_policy_decision_is_mime_type_supported(response) && !attachment) return FALSE;
        webkit_policy_decision_ignore(decision);
        setError(text("downloads"));
        return TRUE;
    }
    auto* navigation = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const std::string url = webkit_uri_request_get_uri(webkit_navigation_action_get_request(navigation));
    if (debug) g_printerr("launchme-browser: policy %d %s (type %d, gesture %d, own %s)\n", type, url.c_str(),
        webkit_navigation_action_get_navigation_type(navigation), webkit_navigation_action_is_user_gesture(navigation), app.own_load.c_str());
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        // target=_blank and window.open: a link the user followed opens in this window.
        webkit_policy_decision_ignore(decision);
        if (webkit_navigation_action_is_user_gesture(navigation) && protocol::supportedUrl(url)) navigate(url);
        return TRUE;
    }
    if (!app.own_load.empty() && url == app.own_load) { app.own_load.clear(); webkit_policy_decision_use(decision); return TRUE; }
    if (playerUrl(url)) {
        webkit_policy_decision_ignore(decision);
        loadPage(url);
        return TRUE;
    }
    if (!allowedScheme(url)) {
        webkit_policy_decision_ignore(decision);
        setError(text("unsupported"));
        return TRUE;
    }
    return FALSE;
}

void loadChanged(WebKitWebView* view, WebKitLoadEvent event, gpointer) {
    if (debug) g_printerr("launchme-browser: load %d %s\n", event, webkit_web_view_get_uri(view));
    if (event == WEBKIT_LOAD_STARTED) {
        app.own_load.clear();
        app.requested = false;
        app.document_load = true;
        const char* uri = webkit_web_view_get_uri(view);
        if (app.failed_url.empty() || !uri || app.failed_url != uri) { // Not WebKit's error page for it.
            app.failed_url.clear();
            app.load_failed = false;
            setError({});
        }
        setLoading(true);
    } else if (event == WEBKIT_LOAD_FINISHED) app.document_load = false;
    if (event == WEBKIT_LOAD_FINISHED && !app.requested) {
        // A load cancelled for the one we started next (a player document, a reload) is not the end.
        if (app.load_failed) setError(text("load-failed"));
        setLoading(false);
    }
    if (const char* uri = webkit_web_view_get_uri(view)) setUrl(uri);
    history();
}

gboolean loadFailed(WebKitWebView* view, WebKitLoadEvent event, gchar* uri, GError* error, gpointer) {
    if (debug) g_printerr("launchme-browser: load failed %d %s: %s %d %s\n", event, uri, g_quark_to_string(error->domain), error->code, error->message);
    // A navigation replaced by another one or refused by our own policy is not a failure.
    if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED) ||
        g_error_matches(error, WEBKIT_POLICY_ERROR, WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE) ||
        g_error_matches(error, WEBKIT_POLICY_ERROR, WEBKIT_POLICY_ERROR_CANNOT_SHOW_MIME_TYPE)) return FALSE;
    app.load_failed = true;
    app.failed_url = uri ? uri : "";
    if (!uri || event == WEBKIT_LOAD_COMMITTED) return FALSE; // A document already showing stays.
    // WebKit's error text on the window's colours, in place of the failed page (as its own error
    // page is, keeping back and forward); the footer says what to do.
    std::string message;
    for (const char* ch = error->message; ch && *ch; ++ch) {
        if (*ch == '<') message += "&lt;"; else if (*ch == '>') message += "&gt;"; else if (*ch == '&') message += "&amp;"; else message += *ch;
    }
    const auto page = "<!doctype html><meta charset=\"utf-8\"><style>:root{color-scheme:dark}body{background:#111318;color:#a6aebb;"
                      "font:17px sans-serif;margin:40px}</style><p>" + message + "</p>";
    webkit_web_view_load_alternate_html(view, page.c_str(), uri, nullptr);
    return TRUE;
}

gboolean tlsFailed(WebKitWebView*, gchar* uri, GTlsCertificate*, GTlsCertificateFlags, gpointer) {
    app.load_failed = true;
    app.failed_url = uri ? uri : "";
    return FALSE;
}

// Same-document navigations (history.pushState, back to such an entry) have no load-changed
// events, only is-loading; document loads end with load-changed FINISHED (after load-failed).
void loadingChanged(WebKitWebView* view, GParamSpec*, gpointer) {
    if (debug) g_printerr("launchme-browser: is-loading %d\n", webkit_web_view_is_loading(view));
    if (webkit_web_view_is_loading(view) || app.document_load) return;
    if (app.history_steps) {
        const int step = app.history_steps < 0 ? -1 : 1;
        app.history_steps -= step;
        historyStep(step);
        if (webkit_web_view_is_loading(view)) return;
    }
    if (!app.requested) setLoading(false);
}

void uriChanged(WebKitWebView* view, GParamSpec*, gpointer) {
    // A new address during a load (back, forward, a link) is reported after `loading 1`, so a
    // client waiting for the address and the end of loading never sees the old load's end.
    if (webkit_web_view_is_loading(view)) setLoading(true);
    if (const char* uri = webkit_web_view_get_uri(view)) setUrl(uri);
    history();
}

void titleChanged(WebKitWebView* view, GParamSpec*, gpointer) {
    const char* title = webkit_web_view_get_title(view);
    const std::string value = title ? title : "";
    if (value == app.title) return;
    app.title = value;
    send({"title", value});
    refresh();
}

void processTerminated(WebKitWebView*, WebKitWebProcessTerminationReason, gpointer) {
    app.error = text("crashed");
    send({"error", app.error});
    setLoading(false);
    setStatus("failed");
}

gboolean denyPermission(WebKitWebView*, WebKitPermissionRequest* request, gpointer) {
    webkit_permission_request_deny(request);
    return TRUE;
}

gboolean permissionState(WebKitWebView*, WebKitPermissionStateQuery* query, gpointer) {
    webkit_permission_state_query_finish(query, WEBKIT_PERMISSION_STATE_DENIED);
    return TRUE;
}

gboolean scriptDialog(WebKitWebView*, WebKitScriptDialog* dialog, gpointer) {
    // No page dialogs (WebView2: AreDefaultScriptDialogsEnabled off); leaving a page is allowed.
    if (webkit_script_dialog_get_dialog_type(dialog) == WEBKIT_SCRIPT_DIALOG_BEFORE_UNLOAD_CONFIRM)
        webkit_script_dialog_confirm_set_confirmed(dialog, TRUE);
    return TRUE;
}

gboolean refuse(gpointer, gpointer, gpointer) { return TRUE; }
gboolean refuseContextMenu(WebKitWebView*, WebKitContextMenu*, WebKitHitTestResult*, gpointer) { return TRUE; }
gboolean fullscreenChanged(WebKitWebView*, gpointer entering) { app.fullscreen = GPOINTER_TO_INT(entering) != 0; return FALSE; }

void downloadStarted(WebKitNetworkSession*, WebKitDownload* download, gpointer) {
    webkit_download_cancel(download);
    setError(text("downloads"));
}

void backForwardChanged(WebKitBackForwardList*, WebKitBackForwardListItem*, gpointer, gpointer) { history(); }

// ---- The window ---------------------------------------------------------------------------------

gboolean keyPressed(GtkEventControllerKey*, guint key, guint, GdkModifierType state, gpointer) {
    const bool alt = (state & GDK_ALT_MASK) != 0;
    if (key == GDK_KEY_Escape && !app.fullscreen) { closeWindow(true); return TRUE; }
    if (key == GDK_KEY_F5) { reload(); return TRUE; }
    if (alt && (key == GDK_KEY_Left || key == GDK_KEY_KP_Left)) { historyStep(-1); return TRUE; }
    if (alt && (key == GDK_KEY_Right || key == GDK_KEY_KP_Right)) { historyStep(1); return TRUE; }
    return FALSE;
}

gboolean closeRequested(GtkWindow*, gpointer) {
    closeWindow(true);
    return TRUE;
}

void activeChanged(GObject*, GParamSpec*, gpointer) {
    const bool active = gtk_window_is_active(app.window);
    if (active == app.active) return;
    app.active = active;
    send({"focus", active ? "1" : "0"});
}

void clicked(GtkButton* button, gpointer) {
    for (int i = 0; i < button_count; ++i)
        if (GTK_WIDGET(button) == app.buttons[i]) { app.focus = i; accept(); refresh(); return; }
}

void hoverEnter(GtkEventControllerMotion* motion, double, double, gpointer index) {
    (void)motion;
    app.hover = GPOINTER_TO_INT(index);
    refresh();
}

void hoverLeave(GtkEventControllerMotion*, gpointer) {
    app.hover = -1;
    refresh();
}

void stateChanged(GdkSurface* surface, GParamSpec*, gpointer) { surfaceState(surface); }

void realized(GtkWidget* widget, gpointer) {
    GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(widget));
    if (!surface) return;
    g_signal_connect(surface, "notify::state", G_CALLBACK(stateChanged), nullptr);
    const auto& parent = app.options.parent;
#ifdef GDK_WINDOWING_WAYLAND
    if (parent.starts_with("wayland:") && GDK_IS_WAYLAND_TOPLEVEL(surface))
        gdk_wayland_toplevel_set_transient_for_exported(GDK_TOPLEVEL(surface), parent.c_str() + 8);
#endif
#ifdef LAUNCHME_BROWSER_X11
    if (GDK_IS_X11_SURFACE(surface)) {
        Display* display = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surface));
        if (parent.starts_with("x11:")) {
            const auto xid = static_cast<::Window>(std::strtoull(parent.c_str() + 4, nullptr, 10));
            if (xid) XSetTransientForHint(display, gdk_x11_surface_get_xid(surface), xid);
        }
        if (!app.options.activate) gdk_x11_surface_set_user_time(surface, 0);
    }
#endif
}

constexpr char style[] = R"css(
window.launchme-browser, window.launchme-browser .page { background: #111318; color: #f2f3f5; }
window.launchme-browser .bar { background: #111318; min-height: 84px; padding: 0 20px; box-shadow: none; border: none; }
window.launchme-browser .bar button {
    min-height: 48px; padding: 0 18px; border-radius: 24px; border: none; box-shadow: none; outline: none;
    background: #272a31; background-image: none; color: #f2f3f5; font-size: 17px; text-shadow: none;
}
window.launchme-browser .bar button.hover, window.launchme-browser .bar button:hover { background: #42454d; }
window.launchme-browser .bar button.muted { color: #a6aebb; }
window.launchme-browser .bar button.chosen { background: #f2f3f5; color: #111318; }
window.launchme-browser .heading { font-size: 17px; color: #f2f3f5; }
window.launchme-browser .address { font-size: 17px; color: #a6aebb; }
window.launchme-browser .footer { font-size: 17px; color: #a6aebb; min-height: 42px; padding: 0 20px; }
window.launchme-browser .message { font-size: 17px; color: #f2f3f5; background: #111318; padding: 40px; }
)css";

GtkWidget* toolbarButton(ToolbarButton index, const char* label, int width) {
    GtkWidget* button = gtk_button_new_with_label(label);
    gtk_widget_set_size_request(button, width, 48);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_focusable(button, FALSE); // Keyboard focus stays in the page.
    gtk_widget_set_focus_on_click(button, FALSE);
    g_signal_connect(button, "clicked", G_CALLBACK(clicked), nullptr);
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(hoverEnter), GINT_TO_POINTER(index));
    g_signal_connect(motion, "leave", G_CALLBACK(hoverLeave), nullptr);
    gtk_widget_add_controller(button, motion);
    app.buttons[index] = button;
    return button;
}

void createWindow() {
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, nullptr);
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, style);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    const std::filesystem::path profile = app.options.profile;
    app.session = webkit_network_session_new((profile / "data").c_str(), (profile / "cache").c_str());
    WebKitCookieManager* cookies = webkit_network_session_get_cookie_manager(app.session);
    webkit_cookie_manager_set_persistent_storage(cookies, (profile / "data" / "cookies.sqlite").c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    g_signal_connect(app.session, "download-started", G_CALLBACK(downloadStarted), nullptr);
    app.context = webkit_web_context_new();
    if (!app.options.language.empty()) {
        const char* languages[] = {app.options.language.c_str(), nullptr};
        webkit_web_context_set_preferred_languages(app.context, languages);
    }
    app.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", app.context, "network-session", app.session, nullptr));
    WebKitSettings* settings = webkit_web_view_get_settings(app.view);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);
    webkit_settings_set_enable_back_forward_navigation_gestures(settings, TRUE);
    const GdkRGBA background{17 / 255.0f, 19 / 255.0f, 24 / 255.0f, 1.0f};
    webkit_web_view_set_background_color(app.view, &background);
    g_signal_connect(app.view, "decide-policy", G_CALLBACK(decidePolicy), nullptr);
    g_signal_connect(app.view, "load-changed", G_CALLBACK(loadChanged), nullptr);
    g_signal_connect(app.view, "load-failed", G_CALLBACK(loadFailed), nullptr);
    g_signal_connect(app.view, "load-failed-with-tls-errors", G_CALLBACK(tlsFailed), nullptr);
    g_signal_connect(app.view, "notify::uri", G_CALLBACK(uriChanged), nullptr);
    g_signal_connect(app.view, "notify::title", G_CALLBACK(titleChanged), nullptr);
    g_signal_connect(app.view, "notify::is-loading", G_CALLBACK(loadingChanged), nullptr);
    g_signal_connect(app.view, "web-process-terminated", G_CALLBACK(processTerminated), nullptr);
    g_signal_connect(app.view, "permission-request", G_CALLBACK(denyPermission), nullptr);
    g_signal_connect(app.view, "query-permission-state", G_CALLBACK(permissionState), nullptr);
    g_signal_connect(app.view, "script-dialog", G_CALLBACK(scriptDialog), nullptr);
    g_signal_connect(app.view, "context-menu", G_CALLBACK(refuseContextMenu), nullptr);
    g_signal_connect(app.view, "print", G_CALLBACK(refuse), nullptr);
    g_signal_connect(app.view, "show-notification", G_CALLBACK(refuse), nullptr);
    g_signal_connect(app.view, "enter-fullscreen", G_CALLBACK(fullscreenChanged), GINT_TO_POINTER(1));
    g_signal_connect(app.view, "leave-fullscreen", G_CALLBACK(fullscreenChanged), GINT_TO_POINTER(0));
    g_signal_connect(webkit_web_view_get_back_forward_list(app.view), "changed", G_CALLBACK(backForwardChanged), nullptr);

    app.window = GTK_WINDOW(gtk_window_new());
    gtk_widget_add_css_class(GTK_WIDGET(app.window), "launchme-browser");
    gtk_window_set_title(app.window, app.options.title.c_str());
    gtk_window_set_icon_name(app.window, "io.github.uxandai.launchme");
    gtk_window_set_default_size(app.window, app.options.width > 0 ? app.options.width : 1280, app.options.height > 0 ? app.options.height : 800);
    gtk_widget_set_size_request(GTK_WIDGET(app.window), 700, 450); // As WM_GETMINMAXINFO on Windows.
    if (app.options.maximize) gtk_window_maximize(app.window);
    g_signal_connect(app.window, "close-request", G_CALLBACK(closeRequested), nullptr);
    g_signal_connect(app.window, "notify::is-active", G_CALLBACK(activeChanged), nullptr);
    g_signal_connect(app.window, "realize", G_CALLBACK(realized), nullptr);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(keyPressed), nullptr);
    gtk_widget_add_controller(GTK_WIDGET(app.window), keys);

    // The toolbar is the title bar: dragged it moves the window, the desktop keeps the resize edges.
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(bar, "bar");
    gtk_box_append(GTK_BOX(bar), toolbarButton(back_button, "\u2039", 52));
    gtk_box_append(GTK_BOX(bar), toolbarButton(reload_button, "Reload", 138));
    gtk_box_append(GTK_BOX(bar), toolbarButton(play_button, "Play / Pause", 192));
    GtkWidget* titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(titles, TRUE);
    gtk_widget_set_valign(titles, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(titles, 10);
    gtk_widget_set_margin_end(titles, 8);
    app.heading = gtk_label_new("");
    app.address = gtk_label_new("");
    for (GtkWidget* label : {app.heading, app.address}) {
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
        gtk_box_append(GTK_BOX(titles), label);
    }
    gtk_widget_add_css_class(app.heading, "heading");
    gtk_widget_add_css_class(app.address, "address");
    gtk_box_append(GTK_BOX(bar), titles);
    gtk_box_append(GTK_BOX(bar), toolbarButton(close_button, "Close \u00b7 Esc / B", 188));
    GtkWidget* handle = gtk_window_handle_new();
    gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), bar);
    gtk_window_set_titlebar(app.window, handle);

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "page");
    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(app.view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(app.view), TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(app.view));
    app.message = gtk_label_new("");
    gtk_widget_add_css_class(app.message, "message");
    gtk_label_set_wrap(GTK_LABEL(app.message), TRUE);
    gtk_label_set_justify(GTK_LABEL(app.message), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(app.message, GTK_ALIGN_FILL);
    gtk_widget_set_valign(app.message, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.message);
    gtk_box_append(GTK_BOX(page), overlay);
    app.footer = gtk_label_new("");
    gtk_widget_add_css_class(app.footer, "footer");
    gtk_label_set_xalign(GTK_LABEL(app.footer), 0);
    gtk_label_set_ellipsize(GTK_LABEL(app.footer), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(page), app.footer);
    gtk_window_set_child(app.window, page);
    gtk_widget_grab_focus(GTK_WIDGET(app.view));
    applyMemory();
    refresh();
}

bool parse(int argc, char** argv, Options& options, bool& check) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const auto value = [&](std::string& target) { if (i + 1 >= argc) return false; target = argv[++i]; return true; };
        const auto number = [&](int& target) { std::string text; if (!value(text)) return false; target = std::atoi(text.c_str()); return true; };
        if (argument == "--check") check = true;
        else if (argument == "--profile") { if (!value(options.profile)) return false; }
        else if (argument == "--title") { if (!value(options.title)) return false; }
        else if (argument == "--language") { if (!value(options.language)) return false; }
        else if (argument == "--parent") { if (!value(options.parent)) return false; }
        else if (argument == "--width") { if (!number(options.width)) return false; }
        else if (argument == "--height") { if (!number(options.height)) return false; }
        else if (argument == "--maximize") options.maximize = true;
        else if (argument == "--no-activate") options.activate = false;
        else if (argument == "--low-memory") options.low_memory = true;
        else return false;
    }
    return check || (!options.profile.empty() && std::filesystem::path(options.profile).is_absolute());
}
}

int main(int argc, char** argv) {
    bool check = false;
    if (!parse(argc, argv, app.options, check)) {
        std::fprintf(stderr, "usage: launchme-browser --profile <absolute dir> [options]  (driven by launchme)\n");
        return browser_protocol::exit_usage;
    }
    // Linking succeeded when we get here; report the engine for diagnostics.
    if (check) return webkit_get_major_version() >= 2 ? 0 : browser_protocol::exit_unavailable;
    std::signal(SIGPIPE, SIG_IGN);
    // The launcher's socket arrives as stdout: keep it off fd 1 (and out of WebKit's processes).
    struct stat channel_info{};
    if (fstat(STDOUT_FILENO, &channel_info) != 0 || !S_ISSOCK(channel_info.st_mode)) {
        std::fprintf(stderr, "launchme-browser: stdout must be the launcher's socket\n");
        return browser_protocol::exit_usage;
    }
    app.channel = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    if (app.channel < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) return browser_protocol::exit_usage;
    fcntl(app.channel, F_SETFL, fcntl(app.channel, F_GETFL) | O_NONBLOCK);
    app.low_memory = app.options.low_memory;
    // Same application identity as the launcher's SDL window: one icon and group on the desktop.
    g_set_prgname("io.github.uxandai.launchme");
    g_set_application_name("Launchme");
    if (!gtk_init_check()) {
        send({"error", "GTK cannot open the display"});
        return browser_protocol::exit_unavailable;
    }
    send({"hello", std::to_string(getpid()), std::to_string(webkit_get_major_version()) + "." +
          std::to_string(webkit_get_minor_version()) + "." + std::to_string(webkit_get_micro_version())});
    createWindow();
    g_unix_fd_add(app.channel, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), channelReady, nullptr);
    while (!app.done) g_main_context_iteration(nullptr, TRUE);
    // Let WebKit close the page and its processes see the view go (cookies and storage are written
    // by the network process); the window is already hidden and the launcher does not wait.
    if (app.window) gtk_window_destroy(app.window);
    app.window = nullptr;
    const gint64 until = g_get_monotonic_time() + 300 * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time() < until) {
        if (!g_main_context_iteration(nullptr, FALSE)) g_usleep(5 * G_TIME_SPAN_MILLISECOND);
    }
    if (app.fixture_server) g_object_unref(app.fixture_server);
    if (app.channel >= 0) ::close(app.channel);
    return 0;
}
