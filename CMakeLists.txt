cmake_minimum_required(VERSION 3.24)
project(launchme VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This native Direct2D prototype currently targets Windows.")
endif()

# ZIP reader/writer, pinned upstream revision (3.1.0); no external runtime DLL.
include(FetchContent)
# Headers + the small statically linked loader only. The browser engine is the shared,
# automatically serviced Evergreen Runtime, never a bundled Fixed Version/Edge browser.
FetchContent_Declare(webview2
    URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3537.50/microsoft.web.webview2.1.0.3537.50.nupkg
    URL_HASH SHA256=5ea526bbd728adda0da4d31219267e96460494a427e4894c4e09d9f320f4b9aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(webview2)
add_library(launchme_webview2 STATIC IMPORTED GLOBAL)
set_target_properties(launchme_webview2 PROPERTIES
    IMPORTED_LOCATION "${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${webview2_SOURCE_DIR}/build/native/include")
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 174573d60290f447c13a2b1b3405de2b96e27d6c)
FetchContent_MakeAvailable(miniz)

option(LAUNCHME_STATIC_RUNTIME "Link the launcher C++ runtime into the executable" ON)
set(LAUNCHME_RUNTIME "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT LAUNCHME_STATIC_RUNTIME)
    string(APPEND LAUNCHME_RUNTIME "DLL")
endif()
set_property(TARGET miniz PROPERTY MSVC_RUNTIME_LIBRARY "${LAUNCHME_RUNTIME}")

# Only PresentMon's ETW consumer, not its desktop application/service. Pinned MIT
# source; no injected DLL, runtime download or separate installation is needed.
FetchContent_Declare(presentmon GIT_REPOSITORY https://github.com/GameTechDev/PresentMon.git
    GIT_TAG 2ce1158783e570539119f577d894252b395cadca GIT_SUBMODULES "")
FetchContent_MakeAvailable(presentmon)
set(PRESENTMON_DATA "${presentmon_SOURCE_DIR}/PresentData")
add_library(launchme_presentmon STATIC
    "${PRESENTMON_DATA}/Debug.cpp" "${PRESENTMON_DATA}/GpuTrace.cpp"
    "${PRESENTMON_DATA}/MixedRealityTraceConsumer.cpp" "${PRESENTMON_DATA}/PresentMonTraceConsumer.cpp"
    "${PRESENTMON_DATA}/TraceConsumer.cpp" "${PRESENTMON_DATA}/TraceSession.cpp")
target_compile_features(launchme_presentmon PRIVATE cxx_std_20)
target_compile_definitions(launchme_presentmon PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    PUBLIC PRESENTMON_ENABLE_DEBUG_TRACE=0) # PresentEvent layout must agree in Debug too.
set_property(TARGET launchme_presentmon PROPERTY MSVC_RUNTIME_LIBRARY "${LAUNCHME_RUNTIME}")
target_include_directories(launchme_presentmon SYSTEM PUBLIC "${PRESENTMON_DATA}")
target_link_libraries(launchme_presentmon PUBLIC tdh advapi32)

# UI text lookup, compiled once with the common backend sources.
set(LAUNCHME_I18N_SOURCES src/i18n/i18n.cpp src/i18n/strings_en.cpp)

function(launchme_target_defaults name)
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_compile_definitions(${name} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(${name} PRIVATE /W4 /EHsc /utf-8 /MP4)
    # Executables and the shared archive must agree on CRT ownership (/MT or /MD).
    set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY "${LAUNCHME_RUNTIME}")
endfunction()

# launchme_executable(<name> [WIN32] SOURCES <files...> [LIBS <libs...>]
#                     [DEFINES <definitions...>] [TEST <ctest name>])
# Settings shared by every target: C++20, Unicode Win32 headers, /W4, UTF-8 sources and
# the shared backend. TEST registers the executable with CTest under the given name.
function(launchme_executable name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "WIN32" "TEST" "SOURCES;LIBS;DEFINES")
    set(kind)
    if(ARG_WIN32)
        set(kind WIN32)
    endif()
    set(sources ${ARG_SOURCES})
    list(REMOVE_ITEM sources ${LAUNCHME_CORE_SOURCES})
    add_executable(${name} ${kind} ${sources})
    launchme_target_defaults(${name})
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
    target_link_libraries(${name} PRIVATE launchme_core)
    if(ARG_LIBS)
        target_link_libraries(${name} PRIVATE ${ARG_LIBS})
    endif()
    if(ARG_TEST)
        add_test(NAME ${ARG_TEST} COMMAND ${name})
    endif()
endfunction()

# Source groups reused by tools and tests.
set(LAUNCHME_PC_SCAN_SOURCES src/library/pc_platforms.cpp src/library/battlenet.cpp src/library/ubisoft.cpp
    src/library/store_adapter.cpp src/library/steam.cpp src/library/visibility.cpp src/net/json.cpp)
set(LAUNCHME_WEB_SOURCES src/net/http.cpp src/net/json.cpp src/platform/user_config.cpp)
set(LAUNCHME_COVER_SOURCES src/artwork/cover_cache.cpp src/artwork/steamgriddb_search.cpp src/artwork/image_compact.cpp src/artwork/download_art.cpp)
set(LAUNCHME_GOG_INSTALL_SOURCES src/store/gog_install.cpp src/store/gog_install_native.cpp src/net/http_download.cpp)
set(LAUNCHME_EPIC_SOURCES src/store/epic_api.cpp src/store/epic_vault.cpp src/store/epic_service.cpp src/store/epic_legendary.cpp)

# Everything in the launcher except its entry point; launcher_ui_tests compiles the same set.
set(LAUNCHME_APP_SOURCES
    src/app/launcher.cpp
    src/app/library_worker.cpp
    src/app/shelves.cpp
    src/app/collections.cpp
    src/app/steam_sync.cpp
    src/app/versions.cpp
    src/ui/library_manager_renderer.cpp
    src/app/artwork_requests.cpp
    src/app/bulk_covers.cpp
    src/ui/bulk_covers_renderer.cpp
    src/app/navigation.cpp
    src/app/pointer.cpp
    src/app/friends.cpp
    src/app/dashboard.cpp
    src/app/paint.cpp
    src/app/input.cpp
    src/app/keyboard.cpp
    src/app/game_overlay.cpp
    src/app/focus_motion.cpp
    src/app/settings.cpp
    src/app/settings_rows.cpp
    src/app/settings_devices.cpp
    src/app/settings_text.cpp
    src/app/settings_color.cpp
    src/app/input_hints.cpp
    src/app/tour.cpp
    src/app/platform_settings.cpp
    src/app/ratings.cpp
    src/app/game_video.cpp
    src/app/screenscraper_ui.cpp
    src/app/identify.cpp
    src/artwork/identified_artwork.cpp
    src/artwork/bulk_covers.cpp
    src/app/store.cpp
    src/app/plugins.cpp
    src/app/plugin_game_views.cpp
    src/app/plugin_page.cpp
    src/ui/plugin_page_renderer.cpp
    src/app/downloads.cpp
    src/app/glance.cpp
    src/app/companion.cpp
    src/app/companion_picker.cpp
    src/app/companion_events.cpp
    src/app/companion_roulette.cpp
    src/app/companion_mind.cpp
    src/app/companion_guest.cpp
    src/app/companion_voice.cpp
    src/app/game_index.cpp
    src/app/companion_photos.cpp
    src/app/companion_feed.cpp
    src/app/companion_backup.cpp
    src/app/companion_settings.cpp
    src/app/companion_affinity.cpp
    src/app/companion_chat_store.cpp
    src/app/companion_remote.cpp
    src/net/remote_server.cpp
    src/app/desktop.cpp
    src/app/desktop_input.cpp
    src/app/desktop_window.cpp
    src/app/jump_list.cpp
    src/app/desktop_quick.cpp
    src/app/desktop_metadata.cpp
    src/app/desktop_text.cpp
    src/app/desktop_settings.cpp
    src/app/desktop_dialogs.cpp
    src/ui/desktop_renderer.cpp
    src/ui/desktop_popups_renderer.cpp
    src/ui/desktop_settings_renderer.cpp
    src/ui/desktop_details_renderer.cpp
    src/ui/desktop_dialogs_renderer.cpp
    src/ui/desktop_pages_renderer.cpp
    src/ui/brand_icons.cpp
    src/ui/browser_window.cpp
    src/ui/glance_renderer.cpp
    src/ui/companion_renderer.cpp
    src/ui/companion_mind_renderer.cpp
    src/ui/companion_feed_renderer.cpp
    src/ui/companion_marks.cpp
    src/net/glance.cpp
    src/net/steam_upcoming.cpp
    src/net/companion.cpp
    src/net/companion_events.cpp
    src/net/companion_lines.cpp
    src/net/companion_providers.cpp
    src/net/companion_photos.cpp
    src/net/companion_feed.cpp
    src/net/companion_backup.cpp
    src/net/companion_feed_social.cpp
    src/net/companion_tools.cpp
    src/net/companion_links.cpp
    src/net/companion_weather.cpp
    src/net/web_search.cpp
    src/net/companion_memory.cpp
    src/net/companion_cognition.cpp
    src/net/companion_mind.cpp
    src/net/companion_chats.cpp
    src/net/companion_characters.cpp
    src/net/elevenlabs.cpp
    src/ui/downloads_renderer.cpp
    src/plugins/manifest.cpp
    src/plugins/package.cpp
    src/plugins/manager.cpp
    src/app/artwork_browser.cpp
    src/app/pc_platforms.cpp
    src/app/gog.cpp
    src/app/epic.cpp
    src/app/web_imports.cpp
    src/store/web_import.cpp
    src/store/ea_account.cpp
    ${LAUNCHME_EPIC_SOURCES}
    src/app/gog_install.cpp
    src/app/my_games.cpp
    src/app/file_browser.cpp
    src/app/drm_free.cpp
    src/app/installations.cpp
    src/app/screensaver.cpp
    src/store/gog_api.cpp
    src/store/gog_config.cpp
    src/store/gog_vault.cpp
    src/store/gog_service.cpp
    src/store/gog_install.cpp
    src/store/gog_install_native.cpp
    src/net/http_download.cpp
    src/app/devices.cpp
    src/app/headphones.cpp
    src/app/music.cpp
    src/app/crop.cpp
    src/app/library_view.cpp
    src/app/library_preview.cpp
    src/app/xmb.cpp
    src/app/flix.cpp
    src/app/metro.cpp
    src/app/psfive.cpp
    src/app/snitch.cpp
    src/app/achievements.cpp
    src/app/game_sessions.cpp
    src/app/quick_resume.cpp
    src/store/game_details.cpp
    src/store/steam_trailer.cpp
    src/ui/renderer.cpp
    src/ui/renderer_resources.cpp
    src/ui/renderer_text.cpp
    src/ui/background_renderer.cpp
    src/ui/card_renderer.cpp
    src/ui/menus_renderer.cpp
    src/ui/home_chrome_renderer.cpp
    src/ui/friends_renderer.cpp
    src/ui/quick_menu_renderer.cpp
    src/ui/grid_renderer.cpp
    src/ui/library_backdrop_renderer.cpp
    src/ui/exit_renderer.cpp
    src/ui/launch_animation_renderer.cpp
    src/ui/overlay_renderer.cpp
    src/ui/overlay_window.cpp
    src/ui/image_decoder.cpp
    src/ui/settings_renderer.cpp
    src/ui/settings_layout_renderer.cpp
    src/ui/settings_tiles_renderer.cpp
    src/ui/hints_renderer.cpp
    src/ui/tour_renderer.cpp
    src/ui/identify_renderer.cpp
    src/ui/details_renderer.cpp
    src/ui/keyboard_renderer.cpp
    src/ui/artwork_renderer.cpp
    src/ui/device_renderer.cpp
    src/ui/audio_renderer.cpp
    src/ui/notification_renderer.cpp
    src/ui/controller_art.cpp
    src/ui/crop_renderer.cpp
    src/ui/gog_install_renderer.cpp
    src/ui/custom_game_renderer.cpp
    src/ui/file_browser_renderer.cpp
    src/ui/library_renderer.cpp
    src/ui/coverflow_renderer.cpp
    src/ui/shelf_renderer.cpp
    src/ui/xmb_renderer.cpp
    src/ui/flix_renderer.cpp
    src/ui/metro_renderer.cpp
    src/ui/psfive_renderer.cpp
    src/ui/snitch_renderer.cpp
    src/ui/glass_renderer.cpp
    src/ui/achievements_renderer.cpp
    src/ui/dashboard_renderer.cpp
    src/ui/screensaver_renderer.cpp
    src/library/steam.cpp
    src/library/game_ratings.cpp
    src/library/rawg.cpp
    src/library/steam_stats.cpp
    src/library/binary_kv.cpp
    src/library/steam_achievements.cpp
    src/library/steam_appinfo.cpp
    src/library/game_index.cpp
    src/library/game_index_sources.cpp
    src/library/game_index_service.cpp
    src/library/steam_tags_builtin.cpp
    src/library/view_options.cpp
    src/library/store_adapter.cpp
    src/library/custom_games.cpp
    src/library/metadata_overrides.cpp
    src/library/drm_free.cpp
    src/library/drm_free_titles.cpp
    src/library/pc_platforms.cpp
    src/library/battlenet.cpp
    src/library/ubisoft.cpp
    src/library/ubisoft_catalog.cpp
    src/library/visibility.cpp
    src/library/collections.cpp
    src/library/organization.cpp
    src/library/steam_sync_state.cpp
    src/library/versions.cpp
    src/library/owned_games.cpp
    src/library/steam_owned.cpp
    src/library/shortcuts.cpp
    src/library/esde_gamelist.cpp
    src/library/screenscraper_config.cpp
    src/library/screenscraper.cpp
    ${LAUNCHME_COVER_SOURCES}
    src/artwork/steamgriddb.cpp
    src/artwork/platform_logo.cpp
    src/artwork/esde_art.cpp
    src/artwork/screenscraper_art.cpp
    src/artwork/text_crop.cpp
    src/artwork/failure_cache.cpp
    src/net/http.cpp
    src/net/json.cpp
    src/friends/steam_friends.cpp
    src/platform/audio.cpp
    src/platform/flac.cpp
    src/platform/system_audio.cpp
    src/platform/volume_monitor.cpp
    src/platform/game_controllers.cpp
    src/platform/bluetooth_controllers.cpp
    src/platform/steam_controller.cpp
    src/platform/device_service.cpp
    src/platform/user_config.cpp
    src/platform/library_state.cpp
    src/platform/launch.cpp
    src/platform/file_dialog.cpp
    src/platform/file_listing.cpp
    src/platform/performance.cpp
    src/platform/frame_clock.cpp
    src/platform/game_activity.cpp
    src/platform/game_overlay_monitor.cpp
    src/platform/layered_surface.cpp
    src/platform/process_freeze.cpp
    src/platform/steam_downloads.cpp
    src/platform/steam_library_sync.cpp
    src/platform/power.cpp
    src/platform/sensors.cpp
    src/platform/discord.cpp
    src/platform/video_player.cpp
)
set(LAUNCHME_APP_LIBS d2d1 dxguid dwrite windowscodecs winhttp shell32 ole32 xaudio2 dwmapi xinput advapi32
    mfplat mfreadwrite mfuuid crypt32 windowsapp hid propsys bcrypt powrprof pdh bthprops cfgmgr32 xmllite shlwapi
    launchme_webview2 version oleaut32 normaliz)

# Backend/parser/platform sources have identical definitions in production, tests
# and tools. App/UI stay separate: LAUNCHME_UI_TEST changes Renderer's class layout.
set(LAUNCHME_CORE_SOURCES ${LAUNCHME_APP_SOURCES})
list(FILTER LAUNCHME_CORE_SOURCES EXCLUDE REGEX "^src/(app|ui)/")
list(APPEND LAUNCHME_CORE_SOURCES ${LAUNCHME_I18N_SOURCES})
list(REMOVE_DUPLICATES LAUNCHME_CORE_SOURCES)
add_library(launchme_core STATIC ${LAUNCHME_CORE_SOURCES})
launchme_target_defaults(launchme_core)
target_link_libraries(launchme_core PUBLIC ${LAUNCHME_APP_LIBS} miniz launchme_presentmon iphlpapi ws2_32)
target_precompile_headers(launchme_core PRIVATE <Windows.h> <string> <vector> <filesystem>)
launchme_executable(steam_library_sync_probe SOURCES tools/steam_library_sync_probe.cpp)

launchme_executable(launchme WIN32 SOURCES src/main.cpp ${LAUNCHME_APP_SOURCES} LIBS ${LAUNCHME_APP_LIBS})
target_precompile_headers(launchme PRIVATE src/app/launcher.hpp)
# Icon, VERSIONINFO (from project()) and the application manifest (PerMonitorV2,
# long paths, Windows 10/11) belong only to the shipped executable.
set(LAUNCHME_RES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res")
configure_file(res/launchme.rc.in "${CMAKE_CURRENT_BINARY_DIR}/launchme.rc" @ONLY)
target_sources(launchme PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/launchme.rc" res/launchme.manifest)
# The companions (res/companion/README.md): one folder per character with its character.txt and
# one folder per costume:
#   res/companion/<key>/character.txt
#   res/companion/<key>/costume_<NN>[_<name>]/<emotion>[_talk|_blink].png
# Characters come in the order of their `order=` line (then by key); NN is the level that unlocks a
# costume with gamification on; costume_00 is the basic outfit (slot 0). Pictures are embedded as
# RCDATA under companion_ui::spriteId: 2000 + ((character * 32 + slot) * 3 + frame) * 32 + emotion
# (emotion order: companion::Emotion, up to 32; at most 20 characters of 32 costumes). Every character.txt
# with its costumes becomes generated/companion_manifest.inc, compiled into
# src/net/companion_characters.cpp (companion_characters.hpp).
# A character's index here is her companion::Look and picks her pictures, so a folder the app would
# not take is skipped here too (with a warning), never later: a key that is not 1–32 of a-z 0-9 _ -,
# starts with _ or -, or names a line of companion_affinity.txt (day, streak, pick, card); no name=; no
# costume_00/cool.png (every missing picture ends at her basic outfit's calm face) unless status=soon.
file(GLOB LAUNCHME_COMPANION_SPRITES CONFIGURE_DEPENDS "${LAUNCHME_RES_DIR}/companion/*/*/*.png")
file(GLOB LAUNCHME_COMPANION_CHARACTERS CONFIGURE_DEPENDS "${LAUNCHME_RES_DIR}/companion/*/character.txt")
file(GLOB LAUNCHME_COMPANION_COSTUME_FILES CONFIGURE_DEPENDS "${LAUNCHME_RES_DIR}/companion/*/costume_*/costume.txt"
     "${LAUNCHME_RES_DIR}/companion/*/costume_*/faces.txt") # A new one reconfigures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LAUNCHME_COMPANION_CHARACTERS}) # Edits reconfigure too.
string(ASCII 239 187 191 LAUNCHME_UTF8_BOM)
# A text file of hers without the UTF-8 BOM Notepad may put in front of the first line.
function(launchme_companion_read file out)
    file(READ "${file}" text)
    string(FIND "${text}" "${LAUNCHME_UTF8_BOM}" bom)
    if(bom EQUAL 0)
        string(SUBSTRING "${text}" 3 -1 text)
    endif()
    set(${out} "${text}" PARENT_SCOPE)
endfunction()
set(LAUNCHME_COMPANION_ORDER "")
foreach(file IN LISTS LAUNCHME_COMPANION_CHARACTERS)
    get_filename_component(folder "${file}" DIRECTORY)
    get_filename_component(look "${folder}" NAME)
    string(LENGTH "${look}" look_length)
    if(NOT look MATCHES "^[a-z0-9][a-z0-9_-]*$" OR look_length GREATER 32)
        message(WARNING "res/companion/${look}: a character's folder is its key (1-32 lowercase letters, digits, _ or -); skipped")
        continue()
    endif()
    if(look STREQUAL "day" OR look STREQUAL "streak" OR look STREQUAL "pick" OR look STREQUAL "card")
        message(WARNING "res/companion/${look}: '${look}' is a line of companion_affinity.txt, not a character's key; skipped")
        continue()
    endif()
    launchme_companion_read("${file}" character)
    # Lines as the app reads them (src/net/companion_characters.cpp): key and value trimmed.
    if(NOT "\n${character}" MATCHES "\n[ \t]*name[ \t]*=[ \t]*[^ \t\r\n]")
        message(WARNING "res/companion/${look}/character.txt: no name= line; skipped")
        continue()
    endif()
    if(NOT "\n${character}\n" MATCHES "\n[ \t]*status[ \t]*=[ \t]*(soon|wip)[ \t\r]*\n" AND
       NOT "${LAUNCHME_RES_DIR}/companion/${look}/costume_00/cool.png" IN_LIST LAUNCHME_COMPANION_SPRITES)
        message(WARNING "res/companion/${look}: costume_00/cool.png is missing (her basic outfit's calm face, where every missing picture ends); skipped")
        continue()
    endif()
    set(order 999)
    if("\n${character}" MATCHES "\n[ \t]*order[ \t]*=[ \t]*([0-9]+)")
        string(LENGTH "${CMAKE_MATCH_1}" digits)
        if(digits GREATER 5)
            set(order 99999)
        else()
            math(EXPR order "${CMAKE_MATCH_1} + 0")
        endif()
    endif()
    math(EXPR order "${order} + 100000") # Same width for sorting (100000-199999).
    list(APPEND LAUNCHME_COMPANION_ORDER "${order}|${look}")
endforeach()
list(SORT LAUNCHME_COMPANION_ORDER)
set(LAUNCHME_COMPANION_RC "// Generated by CMakeLists.txt from res/companion/.\n")
set(LAUNCHME_COMPANION_MANIFEST "// Generated by CMakeLists.txt from res/companion/*/character.txt and the costume folders.\n")
set(LAUNCHME_COMPANION_LOOK 0)
foreach(entry IN LISTS LAUNCHME_COMPANION_ORDER)
    string(REGEX REPLACE "^[0-9]+\\|" "" look "${entry}")
    if(LAUNCHME_COMPANION_LOOK GREATER_EQUAL 20)
        message(WARNING "res/companion/${look}: at most 20 characters")
        break()
    endif()
    launchme_companion_read("${LAUNCHME_RES_DIR}/companion/${look}/character.txt" character)
    set(section "[${look}]\n${character}\n")
    file(GLOB costume_folders LIST_DIRECTORIES true RELATIVE "${LAUNCHME_RES_DIR}/companion/${look}" "${LAUNCHME_RES_DIR}/companion/${look}/costume_*")
    list(SORT costume_folders)
    set(next_slot 1)
    foreach(costume IN LISTS costume_folders)
        if(NOT IS_DIRECTORY "${LAUNCHME_RES_DIR}/companion/${look}/${costume}")
            continue()
        endif()
        if(NOT costume MATCHES "^costume_([0-9][0-9])(_[A-Za-z0-9_-]+)?$")
            message(WARNING "res/companion/${look}/${costume}: expected costume_<NN>[_<name>] (NN = 00-50, name: letters, digits, _ or -)")
            continue()
        endif()
        math(EXPR costume_level "1${CMAKE_MATCH_1} - 100") # "07" -> 7 (no octal or empty strings).
        if(costume_level GREATER 50)
            message(WARNING "res/companion/${look}/${costume}: level ${costume_level} is past the top level (50); skipped")
            continue()
        endif()
        if(costume STREQUAL "costume_00")
            set(slot 0)
        elseif(next_slot GREATER_EQUAL 32)
            message(WARNING "res/companion/${look}/${costume}: at most 32 costumes")
            continue()
        else()
            set(slot ${next_slot})
            math(EXPR next_slot "${next_slot} + 1")
        endif()
        string(APPEND section "costume=${slot} ${costume_level} ${costume}\n")
        # Its optional costume.txt (name_pl, name_en, you_pl, you_en) and faces.txt (face_<emotion>, written by
        # tools/companion_faces.py): "costume." lines after it.
        foreach(costume_name costume.txt faces.txt)
            set(costume_file "${LAUNCHME_RES_DIR}/companion/${look}/${costume}/${costume_name}")
            if(NOT EXISTS "${costume_file}")
                continue()
            endif()
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${costume_file}")
            file(STRINGS "${costume_file}" costume_lines ENCODING UTF-8)
            foreach(costume_line IN LISTS costume_lines)
                string(FIND "${costume_line}" "${LAUNCHME_UTF8_BOM}" bom)
                if(bom EQUAL 0)
                    string(SUBSTRING "${costume_line}" 3 -1 costume_line)
                endif()
                string(STRIP "${costume_line}" costume_line)
                if(costume_line MATCHES "^[a-z_]+[ \t]*=")
                    string(APPEND section "costume.${costume_line}\n")
                endif()
            endforeach()
        endforeach()
        set(LAUNCHME_COMPANION_FRAME 0)
        foreach(frame "" _talk _blink)
            set(LAUNCHME_COMPANION_EMOTION 0)
            foreach(emotion smile angry sad excited shock thinking smug shy fear sleepy playful cool love laugh cry confused determined wave drink gaming)
                set(sprite "${LAUNCHME_RES_DIR}/companion/${look}/${costume}/${emotion}${frame}.png")
                if(sprite IN_LIST LAUNCHME_COMPANION_SPRITES)
                    math(EXPR id "2000 + ((${LAUNCHME_COMPANION_LOOK} * 32 + ${slot}) * 3 + ${LAUNCHME_COMPANION_FRAME}) * 32 + ${LAUNCHME_COMPANION_EMOTION}")
                    string(APPEND LAUNCHME_COMPANION_RC "${id} RCDATA \"${sprite}\"\n")
                endif()
                math(EXPR LAUNCHME_COMPANION_EMOTION "${LAUNCHME_COMPANION_EMOTION} + 1")
            endforeach()
            math(EXPR LAUNCHME_COMPANION_FRAME "${LAUNCHME_COMPANION_FRAME} + 1")
        endforeach()
        # Her full-figure pose in this costume (pose.png, optional): shown as the chat opens, under
        # companion_ui::poseId = 64000 + character * 32 + slot (above every sprite ID).
        set(pose "${LAUNCHME_RES_DIR}/companion/${look}/${costume}/pose.png")
        if(pose IN_LIST LAUNCHME_COMPANION_SPRITES)
            math(EXPR id "64000 + ${LAUNCHME_COMPANION_LOOK} * 32 + ${slot}")
            string(APPEND LAUNCHME_COMPANION_RC "${id} RCDATA \"${pose}\"\n")
        endif()
    endforeach()
    # Raw literals of at most 8000 bytes side by side (MSVC limits each literal, not what they join
    # into); an @ of hers stays an @ through file(CONFIGURE ... @ONLY) below.
    string(LENGTH "${section}" section_length)
    set(at 0)
    while(at LESS section_length)
        string(SUBSTRING "${section}" ${at} 8000 chunk)
        math(EXPR rest "${section_length} - ${at}")
        if(rest GREATER 8000)
            # Cut after the chunk's last line break: a cut by bytes could split a UTF-8 character.
            string(FIND "${chunk}" "\n" newline REVERSE)
            if(newline GREATER 0)
                math(EXPR keep "${newline} + 1")
                string(SUBSTRING "${chunk}" 0 ${keep} chunk)
            endif()
        endif()
        string(LENGTH "${chunk}" chunk_length)
        string(REPLACE "@" "@LAUNCHME_AT@" chunk "${chunk}")
        string(APPEND LAUNCHME_COMPANION_MANIFEST "R\"companion(${chunk})companion\"\n")
        math(EXPR at "${at} + ${chunk_length}")
    endwhile()
    math(EXPR LAUNCHME_COMPANION_LOOK "${LAUNCHME_COMPANION_LOOK} + 1")
endforeach()
if(LAUNCHME_COMPANION_LOOK EQUAL 0)
    string(APPEND LAUNCHME_COMPANION_MANIFEST "\"\"\n")
endif()
set(LAUNCHME_AT "@")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/companion_manifest.inc" CONTENT "${LAUNCHME_COMPANION_MANIFEST}" @ONLY)
target_include_directories(launchme_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Phone access (src/app/companion_remote.cpp): her page, served from the program itself.
string(APPEND LAUNCHME_COMPANION_RC "65000 RCDATA \"${LAUNCHME_RES_DIR}/remote/index.html\"\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/companion.rc" CONTENT "${LAUNCHME_COMPANION_RC}")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/companion.rc" PROPERTIES OBJECT_DEPENDS "${LAUNCHME_COMPANION_SPRITES};${LAUNCHME_RES_DIR}/remote/index.html")
target_sources(launchme PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/companion.rc")
install(TARGETS launchme RUNTIME DESTINATION . COMPONENT Runtime)
add_custom_target(launchme_audio ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LAUNCHME_RES_DIR}/HorizonsTheme.mp3" "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/HorizonsTheme.mp3"
    DEPENDS "${LAUNCHME_RES_DIR}/HorizonsTheme.mp3" VERBATIM)
add_dependencies(launchme launchme_audio)
install(FILES res/HorizonsTheme.mp3 DESTINATION assets COMPONENT Runtime)

# Bundled extensions are separate DLL/ZIPs, built by the normal launcher target.
# Packaging also runs for manifest-only edits, without relying on a DLL relink.
# COMPONENT selects the install component (default Runtime, the published build).
function(launchme_plugin target folder id dll)
    cmake_parse_arguments(PARSE_ARGV 4 ARG "" "COMPONENT" "HELPERS;ASSETS")
    if(NOT ARG_COMPONENT)
        set(ARG_COMPONENT Runtime)
    endif()
    add_library(${target} SHARED "${folder}/plugin.cpp" ${ARG_UNPARSED_ARGUMENTS})
    launchme_target_defaults(${target})
    target_compile_definitions(${target} PRIVATE LAUNCHME_PLUGIN_EXPORTS)
    target_link_libraries(${target} PRIVATE launchme_core)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${dll}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugin-stage/${dll}")
    set(helper_commands)
    set(helper_files)
    foreach(helper IN LISTS ARG_HELPERS)
        list(APPEND helper_commands COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${helper}>" "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:${helper}>")
        list(APPEND helper_files "$<TARGET_FILE_NAME:${helper}>")
    endforeach()
    foreach(asset IN LISTS ARG_ASSETS)
        list(APPEND helper_commands COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/${asset}" "$<TARGET_FILE_DIR:${target}>/${asset}")
    endforeach()
    add_custom_target(${target}_package ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/plugin.json" "$<TARGET_FILE_DIR:${target}>/plugin.json"
        ${helper_commands}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/plugin-packages"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/plugin-packages/${id}.zip" --format=zip -- plugin.json "${dll}.dll" ${helper_files} ${ARG_ASSETS}
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>"
        DEPENDS ${target} ${ARG_HELPERS} "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/plugin.json" VERBATIM)
    add_dependencies(launchme ${target}_package)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/plugin-packages/${id}.zip" DESTINATION plugin-packages COMPONENT ${ARG_COMPONENT})
endfunction()
launchme_plugin(launchme_howlongtobeat plugins/howlongtobeat launchme.howlongtobeat howlongtobeat plugins/howlongtobeat/hltb.cpp plugins/howlongtobeat/service.cpp)
install(FILES "${miniz_SOURCE_DIR}/LICENSE" DESTINATION plugin-packages/licenses RENAME miniz.txt COMPONENT Runtime)
install(FILES "${presentmon_SOURCE_DIR}/LICENSE.txt" DESTINATION plugin-packages/licenses RENAME presentmon.txt COMPONENT Runtime)
install(FILES "${webview2_SOURCE_DIR}/LICENSE.txt" DESTINATION plugin-packages/licenses RENAME webview2.txt COMPONENT Runtime)

# Developer tools (not installed).
launchme_executable(download_art_probe SOURCES tools/download_art_probe.cpp)
launchme_executable(disc_art_probe SOURCES tools/disc_art_probe.cpp)
launchme_executable(game_overlay_probe SOURCES tools/game_overlay_probe.cpp LIBS d3d11 dxgi)
launchme_executable(drm_free_probe SOURCES tools/drm_free_probe.cpp)
launchme_executable(ratings_probe
    SOURCES tools/ratings_probe.cpp src/library/game_ratings.cpp src/library/rawg.cpp src/artwork/identified_artwork.cpp
        src/artwork/steamgriddb.cpp ${LAUNCHME_COVER_SOURCES} ${LAUNCHME_WEB_SOURCES}
    LIBS windowscodecs winhttp shell32 ole32)
launchme_executable(rebuild_crops
    SOURCES tools/rebuild_crops.cpp src/artwork/text_crop.cpp src/library/shortcuts.cpp
        ${LAUNCHME_PC_SCAN_SOURCES} src/platform/user_config.cpp
    LIBS windowscodecs shell32 ole32 advapi32)

include(CTest)
launchme_executable(battlenet_install_probe
    SOURCES tools/battlenet_install_probe.cpp ${LAUNCHME_PC_SCAN_SOURCES}
    LIBS advapi32 shell32)
launchme_executable(gog_client_setup
    SOURCES tools/gog_client_setup.cpp src/store/gog_config.cpp src/store/gog_api.cpp
        ${LAUNCHME_WEB_SOURCES} ${LAUNCHME_GOG_INSTALL_SOURCES}
    LIBS winhttp crypt32 shell32 ole32 bcrypt)
launchme_executable(gog_account_probe
    SOURCES tools/gog_account_probe.cpp src/store/gog_config.cpp src/store/gog_api.cpp src/store/gog_vault.cpp
        ${LAUNCHME_WEB_SOURCES} ${LAUNCHME_GOG_INSTALL_SOURCES}
    LIBS winhttp crypt32 shell32 ole32 bcrypt)
launchme_executable(image_probe
    SOURCES tools/image_probe.cpp src/net/http.cpp
    LIBS windowscodecs winhttp ole32)
launchme_executable(epic_backend_probe
    SOURCES tools/epic_backend_probe.cpp ${LAUNCHME_EPIC_SOURCES} ${LAUNCHME_WEB_SOURCES} src/net/http_download.cpp
    LIBS winhttp crypt32 bcrypt shell32 ole32 advapi32)

# Tests. Targets without TEST (*_live_test) touch the real machine or network and are run by hand.
launchme_executable(companion_backup SOURCES tools/companion_backup.cpp)
if(BUILD_TESTING)
    launchme_executable(browser_window_tests TEST evergreen_browser_lifecycle
        SOURCES tests/browser_window_tests.cpp src/ui/browser_window.cpp DEFINES LAUNCHME_UI_TEST)
    set_tests_properties(evergreen_browser_lifecycle PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
    launchme_executable(glance_tests TEST glance_sources_and_cache SOURCES tests/glance_tests.cpp)
    launchme_executable(companion_tests TEST companion_replies_prompts_and_rig SOURCES tests/companion_tests.cpp)
    launchme_executable(companion_links_tests TEST companion_bounded_link_previews SOURCES tests/companion_links_tests.cpp)
    launchme_executable(companion_feed_tests TEST companion_feed_publication_and_archive SOURCES tests/companion_feed_tests.cpp)
    launchme_executable(remote_server_tests TEST remote_server_parsing_and_loopback SOURCES tests/remote_server_tests.cpp)
    launchme_executable(companion_backup_tests TEST companion_portable_backup SOURCES tests/companion_backup_tests.cpp)
    launchme_executable(companion_feelings_tests TEST companion_named_feelings SOURCES tests/companion_feelings_tests.cpp)
    launchme_executable(companion_reply_style_tests TEST companion_reply_length_and_history SOURCES tests/companion_reply_style_tests.cpp)
    launchme_executable(game_index_tests TEST game_index_sources_queries_and_tools SOURCES tests/game_index_tests.cpp)
    launchme_executable(bulk_covers_tests TEST bulk_covers_missing_only_and_limits SOURCES tests/bulk_covers_tests.cpp)
    launchme_executable(steam_library_sync_tests TEST steam_library_sync_outbox SOURCES tests/steam_library_sync_tests.cpp)
    find_program(LAUNCHME_NODE_EXECUTABLE node)
    if(LAUNCHME_NODE_EXECUTABLE)
        add_test(NAME steam_library_sync_bridge COMMAND "${LAUNCHME_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/steam_library_bridge_tests.mjs")
    endif()
    launchme_executable(download_art_tests TEST download_artwork_sources_and_cache SOURCES tests/download_art_tests.cpp)
    launchme_executable(howlongtobeat_tests SOURCES tests/howlongtobeat_tests.cpp plugins/howlongtobeat/hltb.cpp)
    add_dependencies(howlongtobeat_tests launchme_howlongtobeat)
    add_test(NAME howlongtobeat_search_and_plugin_cache COMMAND howlongtobeat_tests --plugin $<TARGET_FILE:launchme_howlongtobeat>)
    launchme_executable(game_fps_tests TEST game_fps_history_and_overlay_bar SOURCES tests/game_fps_tests.cpp)
    foreach(fixture plugin_fixture plugin_bad_abi)
        add_library(${fixture} SHARED tests/plugin_fixture.cpp)
        launchme_target_defaults(${fixture})
        target_compile_definitions(${fixture} PRIVATE LAUNCHME_PLUGIN_EXPORTS)
        target_link_libraries(${fixture} PRIVATE launchme_core)
    endforeach()
    target_compile_definitions(plugin_bad_abi PRIVATE BAD_PLUGIN_ABI)
    launchme_executable(plugin_tests SOURCES tests/plugin_tests.cpp)
    add_dependencies(plugin_tests plugin_fixture plugin_bad_abi)
    add_test(NAME native_plugin_package_lifecycle COMMAND plugin_tests $<TARGET_FILE:plugin_fixture> $<TARGET_FILE:plugin_bad_abi>)
    launchme_executable(worker_messages_tests TEST worker_message_ownership_and_cleanup
        SOURCES tests/worker_messages_tests.cpp)
    launchme_executable(volume_monitor_tests TEST off_thread_system_volume
        SOURCES tests/volume_monitor_tests.cpp)
    launchme_executable(text_layout_cache_tests TEST cached_text_pixel_equivalence
        SOURCES tests/text_layout_cache_tests.cpp)
    launchme_executable(library_preview_tests TEST library_preview_lifecycle
        SOURCES tests/library_preview_tests.cpp src/app/library_preview.cpp src/store/game_details.cpp
            src/library/steam.cpp src/library/visibility.cpp src/library/steam_stats.cpp src/library/steam_achievements.cpp ${LAUNCHME_WEB_SOURCES}
        LIBS advapi32 winhttp shell32 ole32)
    launchme_executable(identified_artwork_tests TEST rawg_identification_missing_artwork
        SOURCES tests/identified_artwork_tests.cpp src/artwork/identified_artwork.cpp src/artwork/steamgriddb.cpp
            ${LAUNCHME_COVER_SOURCES} ${LAUNCHME_WEB_SOURCES}
        LIBS windowscodecs winhttp shell32 ole32)
    launchme_executable(game_ratings_tests TEST game_ratings_cache_and_rawg_metadata
        SOURCES tests/game_ratings_tests.cpp src/library/game_ratings.cpp src/library/rawg.cpp ${LAUNCHME_WEB_SOURCES}
        LIBS winhttp shell32 ole32)
    launchme_executable(epic_tests TEST epic_account_and_clientless_contracts
        SOURCES tests/epic_tests.cpp ${LAUNCHME_EPIC_SOURCES} ${LAUNCHME_PC_SCAN_SOURCES} ${LAUNCHME_WEB_SOURCES} src/net/http_download.cpp
        LIBS winhttp crypt32 bcrypt shell32 ole32 advapi32)
    launchme_executable(battlenet_tests TEST battlenet_local_installation_lifecycle
        SOURCES tests/battlenet_tests.cpp ${LAUNCHME_PC_SCAN_SOURCES}
        LIBS advapi32 shell32)
    launchme_executable(gog_install_tests TEST gog_offline_installer_lifecycle
        SOURCES tests/gog_install_tests.cpp ${LAUNCHME_GOG_INSTALL_SOURCES} src/net/json.cpp src/platform/user_config.cpp
        LIBS winhttp bcrypt shell32 ole32)
    launchme_executable(gog_account_tests TEST gog_account_sync_and_privacy
        SOURCES tests/gog_account_tests.cpp src/store/gog_api.cpp src/store/gog_config.cpp src/store/gog_vault.cpp
            src/store/gog_service.cpp ${LAUNCHME_PC_SCAN_SOURCES} ${LAUNCHME_WEB_SOURCES} ${LAUNCHME_GOG_INSTALL_SOURCES}
        LIBS winhttp crypt32 bcrypt shell32 ole32 advapi32)
    set_tests_properties(gog_account_sync_and_privacy epic_account_and_clientless_contracts PROPERTIES TIMEOUT 60)
    launchme_executable(image_validation_tests TEST controller_png_transparency
        SOURCES tests/image_validation_tests.cpp
        LIBS windowscodecs ole32)
    launchme_executable(game_session_tests TEST game_session_music_lifecycle
        SOURCES tests/game_session_tests.cpp src/platform/game_activity.cpp src/platform/process_freeze.cpp
        LIBS advapi32)
    launchme_executable(failure_cache_tests TEST persistent_artwork_failure_cache
        SOURCES tests/failure_cache_tests.cpp src/artwork/failure_cache.cpp)
    launchme_executable(library_state_tests TEST library_view_state_and_sorting
        SOURCES tests/library_state_tests.cpp src/platform/library_state.cpp src/library/view_options.cpp)
    launchme_executable(device_tests TEST device_input_and_audio_transactions
        SOURCES tests/device_tests.cpp src/platform/game_controllers.cpp src/platform/steam_controller.cpp
        LIBS windowsapp hid xinput)
    launchme_executable(controller_models_tests TEST controller_picture_classification
        SOURCES tests/controller_models_tests.cpp)
    launchme_executable(device_live_test
        SOURCES tests/device_live_test.cpp src/platform/device_service.cpp src/platform/game_controllers.cpp
            src/platform/steam_controller.cpp src/platform/system_audio.cpp src/platform/user_config.cpp src/net/http.cpp
        LIBS windowsapp hid xinput windowscodecs winhttp shell32 ole32 propsys)
    launchme_executable(steamgriddb_tests TEST steamgriddb_browser_and_selection
        SOURCES tests/steamgriddb_tests.cpp src/artwork/steamgriddb.cpp ${LAUNCHME_COVER_SOURCES} ${LAUNCHME_WEB_SOURCES}
        LIBS windowscodecs winhttp shell32 ole32)
    launchme_executable(artwork_browser_live_test
        SOURCES tests/artwork_browser_live_test.cpp src/artwork/steamgriddb.cpp ${LAUNCHME_COVER_SOURCES} ${LAUNCHME_WEB_SOURCES}
        LIBS windowscodecs winhttp shell32 ole32)
    launchme_executable(pc_platforms_tests TEST native_pc_platform_libraries
        SOURCES tests/pc_platforms_tests.cpp ${LAUNCHME_PC_SCAN_SOURCES}
        LIBS advapi32 shell32)
    launchme_executable(ubisoft_tests TEST ubisoft_connect_client_library
        SOURCES tests/ubisoft_tests.cpp ${LAUNCHME_PC_SCAN_SOURCES}
        LIBS advapi32 shell32)
    launchme_executable(store_import_tests TEST ea_battlenet_website_imports
        SOURCES tests/store_import_tests.cpp src/store/web_import.cpp src/store/ea_account.cpp ${LAUNCHME_PC_SCAN_SOURCES}
        LIBS advapi32 shell32 crypt32)
    launchme_executable(pc_platforms_live_test
        SOURCES tests/pc_platforms_live_test.cpp ${LAUNCHME_PC_SCAN_SOURCES} src/library/ubisoft_catalog.cpp src/net/http.cpp
            src/platform/user_config.cpp
        LIBS advapi32 shell32 ole32 winhttp)
    launchme_executable(launcher_ui_tests TEST native_settings_store_details_flow
        SOURCES tests/launcher_ui_tests.cpp ${LAUNCHME_APP_SOURCES}
        LIBS ${LAUNCHME_APP_LIBS}
        DEFINES LAUNCHME_UI_TEST)
    target_precompile_headers(launcher_ui_tests PRIVATE src/app/launcher.hpp)
    target_sources(launcher_ui_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/companion.rc")
    add_dependencies(launcher_ui_tests launchme_audio)
    add_test(NAME cover_zoom_launch_effect COMMAND launcher_ui_tests --launch-animation)
    add_test(NAME file_picker_colors_and_glow COMMAND launcher_ui_tests --file-browser)
    add_test(NAME guide_overlay_keyboard_and_navigation COMMAND launcher_ui_tests --guide-overlay)
    add_test(NAME recent_sessions_updates_and_background COMMAND launcher_ui_tests --recent-sessions)
    add_test(NAME library_presentations_and_navigation COMMAND launcher_ui_tests --library-views)
    set_tests_properties(library_presentations_and_navigation PROPERTIES TIMEOUT 60)
    set_tests_properties(guide_overlay_keyboard_and_navigation PROPERTIES TIMEOUT 60)
    add_test(NAME launcher_exit_animation COMMAND launcher_ui_tests --exit-animation)
    set_tests_properties(launcher_exit_animation PROPERTIES TIMEOUT 60)
    add_test(NAME screen_transitions_and_wallpaper COMMAND launcher_ui_tests --screen-transitions)
    set_tests_properties(screen_transitions_and_wallpaper PROPERTIES TIMEOUT 60)
    add_dependencies(launcher_ui_tests launchme_howlongtobeat_package)
    add_test(NAME library_organization_and_versions COMMAND launcher_ui_tests --organization)
    add_test(NAME plugin_library_supplement_merge COMMAND launcher_ui_tests --plugin-library)
    add_test(NAME hltb_details_and_active_game_widget COMMAND launcher_ui_tests --hltb-details)
    add_test(NAME psfive_home_layout COMMAND launcher_ui_tests --psfive)
    add_test(NAME snitch_home_layout COMMAND launcher_ui_tests --snitch)
    set_tests_properties(snitch_home_layout PROPERTIES TIMEOUT 60)
    add_test(NAME glance_shortcut_navigation_and_hints COMMAND launcher_ui_tests --glance)
    set_tests_properties(glance_shortcut_navigation_and_hints PROPERTIES TIMEOUT 60)
    add_test(NAME companion_home_chat_and_settings COMMAND launcher_ui_tests --companion)
    add_test(NAME companion_picker_and_desktop_input COMMAND launcher_ui_tests --companion-appearance)
    add_test(NAME companion_reply_length_ui COMMAND launcher_ui_tests --companion-reply-length)
    add_test(NAME companion_remote_ui COMMAND launcher_ui_tests --companion-remote)
    add_test(NAME companion_link_reading_and_video_ui COMMAND launcher_ui_tests --companion-links)
    add_test(NAME companion_clipboard_input COMMAND launcher_ui_tests --companion-paste)
    set_tests_properties(companion_clipboard_input PROPERTIES RUN_SERIAL TRUE)
    set_tests_properties(companion_home_chat_and_settings PROPERTIES TIMEOUT 90)
    add_test(NAME game_index_details_library_and_tools COMMAND launcher_ui_tests --game-index)
    set_tests_properties(game_index_details_library_and_tools PROPERTIES TIMEOUT 90)
    add_test(NAME home_widgets_in_every_layout COMMAND launcher_ui_tests --home-widgets)
    set_tests_properties(home_widgets_in_every_layout PROPERTIES TIMEOUT 90)
    add_test(NAME interface_refresh_layouts_and_controls COMMAND launcher_ui_tests --ui-refresh)
    set_tests_properties(interface_refresh_layouts_and_controls PROPERTIES TIMEOUT 90)
    add_test(NAME desktop_mode_library_and_dialogs COMMAND launcher_ui_tests --desktop)
    set_tests_properties(desktop_mode_library_and_dialogs PROPERTIES TIMEOUT 90)
    launchme_executable(settings_navigation_tests TEST settings_navigation_and_artwork_viewport
        SOURCES tests/settings_navigation_tests.cpp)
    launchme_executable(store_adapter_tests TEST store_adapters_and_identity
        SOURCES tests/store_adapter_tests.cpp src/library/store_adapter.cpp src/library/steam.cpp src/library/visibility.cpp
            src/library/collections.cpp ${LAUNCHME_COVER_SOURCES} src/artwork/platform_logo.cpp ${LAUNCHME_WEB_SOURCES}
        LIBS advapi32 winhttp shell32 ole32)
    launchme_executable(drm_free_tests TEST drm_free_folder_scan_and_matching
        SOURCES tests/drm_free_tests.cpp)
    launchme_executable(custom_games_tests TEST user_added_games
        SOURCES tests/custom_games_tests.cpp src/library/custom_games.cpp src/library/store_adapter.cpp
            src/library/steam.cpp src/library/visibility.cpp src/net/json.cpp
        LIBS bcrypt advapi32 shell32)
    launchme_executable(steam_tests TEST steam_library_scan
        SOURCES tests/steam_tests.cpp src/library/steam.cpp src/library/steam_stats.cpp src/library/steam_achievements.cpp
            src/library/visibility.cpp src/net/json.cpp src/net/http.cpp
        LIBS advapi32 winhttp)
    launchme_executable(steam_install_live_test
        SOURCES tests/steam_install_live_test.cpp src/library/steam.cpp src/library/visibility.cpp src/platform/steam_downloads.cpp
        LIBS advapi32)
    launchme_executable(text_crop_tests TEST text_aware_crop
        SOURCES tests/text_crop_tests.cpp src/artwork/text_crop.cpp
        LIBS windowscodecs ole32)
    launchme_executable(visibility_tests TEST steam_visibility
        SOURCES tests/visibility_tests.cpp src/library/visibility.cpp)
    launchme_executable(artwork_live_test
        SOURCES tests/artwork_live_test.cpp ${LAUNCHME_COVER_SOURCES} src/artwork/platform_logo.cpp src/artwork/esde_art.cpp src/library/esde_gamelist.cpp
            ${LAUNCHME_WEB_SOURCES}
        LIBS winhttp shell32 ole32)
    launchme_executable(soundtrack_tests TEST native_flac_decode
        SOURCES tests/soundtrack_tests.cpp src/platform/flac.cpp
        LIBS mfplat mfreadwrite mfuuid shell32 ole32)
    add_dependencies(soundtrack_tests launchme_audio)
    launchme_executable(collections_tests TEST steam_collections
        SOURCES tests/collections_tests.cpp src/library/collections.cpp src/library/visibility.cpp src/net/json.cpp)
    launchme_executable(friends_layout_tests TEST friends_sidebar_navigation
        SOURCES tests/friends_layout_tests.cpp)
    launchme_executable(sensors_tests TEST dashboard_temperature_sensors
        SOURCES tests/sensors_tests.cpp src/platform/sensors.cpp
        LIBS pdh)
    launchme_executable(user_settings_tests TEST drawer_appearance_settings
        SOURCES tests/user_settings_tests.cpp src/platform/user_config.cpp
        LIBS shell32 ole32)
    launchme_executable(owned_games_tests TEST steam_app_names
        SOURCES tests/owned_games_tests.cpp src/library/owned_games.cpp ${LAUNCHME_WEB_SOURCES}
        LIBS winhttp shell32 ole32)
    launchme_executable(steam_owned_tests TEST steam_client_owned_library SOURCES tests/steam_owned_tests.cpp)
    launchme_executable(ink_bounds_tests TEST logo_ink_bounds
        SOURCES tests/ink_bounds_tests.cpp)
    launchme_executable(brand_icons_tests TEST brand_marks_and_launchme_logo
        SOURCES tests/brand_icons_tests.cpp src/ui/brand_icons.cpp LIBS d2d1)
    launchme_executable(shortcuts_tests TEST steam_nonsteam_shortcuts
        SOURCES tests/shortcuts_tests.cpp src/library/shortcuts.cpp src/library/visibility.cpp src/library/esde_gamelist.cpp)
    launchme_executable(category_menu_tests TEST long_collection_menu
        SOURCES tests/category_menu_tests.cpp)
    launchme_executable(search_tests TEST live_library_search
        SOURCES tests/search_tests.cpp)
    launchme_executable(esde_art_tests TEST esde_emulation_fanart
        SOURCES tests/esde_art_tests.cpp src/artwork/esde_art.cpp src/artwork/image_compact.cpp src/library/esde_gamelist.cpp
        LIBS windowscodecs ole32)
    launchme_executable(screenscraper_config_tests TEST screenscraper_settings_storage
        SOURCES tests/screenscraper_config_tests.cpp src/library/screenscraper_config.cpp src/platform/user_config.cpp ${LAUNCHME_I18N_SOURCES}
        LIBS shell32 ole32 crypt32)
    launchme_executable(screenscraper_tests TEST screenscraper_rom_identification
        SOURCES tests/screenscraper_tests.cpp src/library/screenscraper.cpp src/library/screenscraper_config.cpp src/library/esde_gamelist.cpp
            src/artwork/screenscraper_art.cpp src/artwork/esde_art.cpp src/artwork/image_compact.cpp src/store/steam_trailer.cpp src/store/game_details.cpp
            ${LAUNCHME_WEB_SOURCES} ${LAUNCHME_I18N_SOURCES}
        LIBS shell32 ole32 crypt32 bcrypt winhttp windowscodecs)
    launchme_executable(video_player_live_test
        SOURCES tests/video_player_live_test.cpp src/platform/video_player.cpp src/store/steam_trailer.cpp src/store/game_details.cpp src/artwork/image_compact.cpp
            ${LAUNCHME_WEB_SOURCES} ${LAUNCHME_I18N_SOURCES}
        LIBS mfplat mfuuid windowscodecs ole32 shell32 winhttp)
    launchme_executable(retry_policy_tests TEST artwork_retry_backoff
        SOURCES tests/retry_policy_tests.cpp)
    launchme_executable(lru_cache_tests TEST artwork_memory_lru
        SOURCES tests/lru_cache_tests.cpp)
    launchme_executable(background_work_tests TEST owned_background_workers
        SOURCES tests/background_work_tests.cpp)
    launchme_executable(screensaver_tests TEST screensaver_idle_playlist_and_input
        SOURCES tests/screensaver_tests.cpp)
    launchme_executable(input_gesture_tests TEST guide_start_hold_gestures
        SOURCES tests/input_gesture_tests.cpp)
    launchme_executable(image_decoder_tests TEST background_image_decoding
        SOURCES tests/image_decoder_tests.cpp src/ui/image_decoder.cpp
        LIBS windowscodecs ole32 user32)
    launchme_executable(steam_live_test
        SOURCES tests/steam_live_test.cpp src/library/steam.cpp src/library/steam_stats.cpp src/library/steam_achievements.cpp
            src/library/visibility.cpp src/library/collections.cpp
            src/library/owned_games.cpp src/friends/steam_friends.cpp ${LAUNCHME_WEB_SOURCES}
        LIBS winhttp shell32 ole32 advapi32)
    launchme_executable(i18n_tests TEST polish_english_translations
        SOURCES tests/i18n_tests.cpp
        DEFINES LAUNCHME_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(background_image_decoding native_settings_store_details_flow PROPERTIES TIMEOUT 120)
endif()

# Optional extras kept outside the repository (plugins, tests); nothing when absent.
include(local/CMakeLists.txt OPTIONAL)
