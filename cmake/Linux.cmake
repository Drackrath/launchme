# Linux build (docs/linux-port.md). Included from the top-level CMakeLists.txt instead of the
# Windows build: the shared core (library scanning, stores, network, companion, i18n) and the
# tests that cover it. The launcher window, renderer and web view are not ported yet.

include(FetchContent)
# Strict ISO C++: GNU mode predefines the macro `unix`, which the companion code uses as a name.
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
find_package(CURL REQUIRED)
find_path(SDL3_INCLUDE_DIR SDL3/SDL.h REQUIRED)
find_library(SDL3_LIBRARY NAMES SDL3 REQUIRED)
find_path(SDL3_TTF_INCLUDE_DIR SDL3_ttf/SDL_ttf.h REQUIRED)
find_library(SDL3_TTF_LIBRARY NAMES SDL3_ttf REQUIRED)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 174573d60290f447c13a2b1b3405de2b96e27d6c)
FetchContent_MakeAvailable(miniz)

set(LAUNCHME_I18N_SOURCES src/i18n/i18n.cpp src/i18n/strings_en.cpp src/i18n/strings_pt_br.cpp src/i18n/strings_es.cpp)

function(launchme_target_defaults name)
    target_compile_features(${name} PRIVATE cxx_std_20)
    # Partial aggregate initialisation ({200}) is the codebase's style.
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wno-missing-field-initializers -finput-charset=UTF-8
        -include "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/crt_compat.hpp")
    target_compile_definitions(${name} PRIVATE LAUNCHME_VERSION="${PROJECT_VERSION}")
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endfunction()

# The core sources ported so far; each moves here once it builds and its tests pass on Linux.
set(LAUNCHME_CORE_SOURCES
    ${LAUNCHME_I18N_SOURCES}
    src/artwork/background_focus.cpp
    src/artwork/battlenet_art.cpp
    src/artwork/block_compression.cpp
    src/artwork/bulk_covers.cpp
    src/artwork/cover_cache.cpp
    src/artwork/download_art.cpp
    src/artwork/esde_art.cpp
    src/artwork/failure_cache.cpp
    src/artwork/gog_art.cpp
    src/artwork/identified_artwork.cpp
    src/artwork/steamgriddb_search.cpp
    src/friends/steam_friends.cpp
    src/library/achievement_history.cpp
    src/library/binary_kv.cpp
    src/library/collections.cpp
    src/library/drm_free_titles.cpp
    src/library/esde_gamelist.cpp
    src/library/game_index.cpp
    src/library/game_index_service.cpp
    src/library/game_index_sources.cpp
    src/library/game_ratings.cpp
    src/library/metadata_overrides.cpp
    src/library/organization.cpp
    src/library/owned_games.cpp
    src/library/play_journal.cpp
    src/library/rawg.cpp
    src/library/screenscraper.cpp
    src/library/screenscraper_config.cpp
    src/library/shortcuts.cpp
    src/library/steam.cpp
    src/library/steam_achievements.cpp
    src/library/steam_appinfo.cpp
    src/library/steam_owned.cpp
    src/library/steam_stats.cpp
    src/library/steam_sync_state.cpp
    src/library/steam_tags_builtin.cpp
    src/library/store_adapter.cpp
    src/library/studios.cpp
    src/library/tarot.cpp
    src/library/ubisoft.cpp
    src/library/ubisoft_catalog.cpp
    src/library/versions.cpp
    src/library/view_options.cpp
    src/library/visibility.cpp
    src/net/companion.cpp
    src/net/companion_chats.cpp
    src/net/companion_cognition.cpp
    src/net/companion_events.cpp
    src/net/companion_feed_social.cpp
    src/net/companion_lines.cpp
    src/net/companion_links.cpp
    src/net/companion_memory.cpp
    src/net/companion_mind.cpp
    src/net/companion_photos.cpp
    src/net/companion_providers.cpp
    src/net/companion_semantic.cpp
    src/net/companion_social.cpp
    src/net/companion_tools.cpp
    src/net/companion_weather.cpp
    src/net/elevenlabs.cpp
    src/net/http.cpp
    src/net/http_curl.cpp
    src/net/http_download.cpp
    src/net/json.cpp
    src/net/steam_upcoming.cpp
    src/net/web_search.cpp
    src/platform/hashing.cpp
    src/platform/library_state.cpp
    src/platform/protected_file.cpp
    src/platform/unicode_text.cpp
    src/platform/user_config.cpp
    src/plugins/achievements.cpp
    src/plugins/manager.cpp
    src/plugins/manifest.cpp
    src/plugins/package.cpp
    src/store/ea_account.cpp
    src/store/epic_api.cpp
    src/store/epic_service.cpp
    src/store/epic_vault.cpp
    src/store/game_details.cpp
    src/store/gog_api.cpp
    src/store/gog_config.cpp
    src/store/gog_install.cpp
    src/store/gog_service.cpp
    src/store/gog_vault.cpp
    src/store/steam_app_match.cpp
    src/store/steam_trailer.cpp
    src/store/web_import.cpp)
set(LAUNCHME_CORE_LIBS miniz Threads::Threads ICU::uc ICU::i18n OpenSSL::Crypto PkgConfig::LIBSECRET CURL::libcurl)

# Sources ported per area (docs/linux-port.md, phase 1). Each block is edited by one piece of
# work at a time; keep additions (and its find_package/pkg_check_modules and
# list(APPEND LAUNCHME_CORE_LIBS ...) lines) inside its own block.

# Processes: launching games and store clients, suspending them, /proc.
set(LAUNCHME_LINUX_PROCESS_SOURCES
    src/platform/launch.cpp
)

# Image decoding and encoding (WIC on Windows).
set(LAUNCHME_LINUX_IMAGE_SOURCES
)

# Sockets, folders and files.
set(LAUNCHME_LINUX_FILES_SOURCES
)

# Game store discovery on Linux (Steam, Heroic, Lutris, Prism Launcher, ...).
set(LAUNCHME_LINUX_STORE_SOURCES
    src/library/battlenet.cpp
    src/library/heroic.cpp
    src/library/lutris.cpp
    src/library/multimc.cpp
    src/library/pc_platforms.cpp
)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
list(APPEND LAUNCHME_CORE_LIBS PkgConfig::SQLITE3)

# Companion assets and embeddings, updater.
set(LAUNCHME_LINUX_COMPANION_SOURCES
)

# System services: memory, sensors, power, Discord, downloads.
set(LAUNCHME_LINUX_SERVICE_SOURCES
)

# Audio and video: playback, FLAC, system volume, trailers.
set(LAUNCHME_LINUX_MEDIA_SOURCES
)

# Game controllers: gamepads, Steam Controller, Bluetooth, device service.
set(LAUNCHME_LINUX_CONTROLLER_SOURCES
)

list(APPEND LAUNCHME_CORE_SOURCES ${LAUNCHME_LINUX_PROCESS_SOURCES} ${LAUNCHME_LINUX_IMAGE_SOURCES}
    ${LAUNCHME_LINUX_FILES_SOURCES} ${LAUNCHME_LINUX_STORE_SOURCES} ${LAUNCHME_LINUX_COMPANION_SOURCES}
    ${LAUNCHME_LINUX_SERVICE_SOURCES} ${LAUNCHME_LINUX_MEDIA_SOURCES} ${LAUNCHME_LINUX_CONTROLLER_SOURCES})

add_library(launchme_core STATIC ${LAUNCHME_CORE_SOURCES})
launchme_target_defaults(launchme_core)
target_include_directories(launchme_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(launchme_core PUBLIC ${LAUNCHME_CORE_LIBS})
get_target_property(miniz_includes miniz INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(miniz PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${miniz_includes}")

add_executable(launchme src/linux/main.cpp src/platform/sdl_window.cpp)
launchme_target_defaults(launchme)
target_include_directories(launchme PRIVATE "${SDL3_INCLUDE_DIR}" "${SDL3_TTF_INCLUDE_DIR}")
target_link_libraries(launchme PRIVATE launchme_core "${SDL3_LIBRARY}" "${SDL3_TTF_LIBRARY}")

# Same signature as the Windows build's helper (the WIN32 flag is ignored).
function(launchme_executable name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "WIN32" "TEST" "SOURCES;LIBS;DEFINES")
    set(sources ${ARG_SOURCES})
    list(REMOVE_ITEM sources ${LAUNCHME_CORE_SOURCES})
    add_executable(${name} ${sources})
    launchme_target_defaults(${name})
    # Tests' Win32 helpers (GetCurrentProcessId, Sleep, ...): tests/linux_compat/Windows.h.
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/linux_compat")
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
    target_link_libraries(${name} PRIVATE launchme_core ${ARG_LIBS})
    if(ARG_TEST)
        add_test(NAME ${ARG_TEST} COMMAND ${name})
    endif()
endfunction()

if(BUILD_TESTING)
    launchme_executable(platform_sdl_window_tests TEST platform_sdl_window_events
        SOURCES tests/platform_sdl_window_tests.cpp src/platform/sdl_window.cpp
        LIBS "${SDL3_LIBRARY}")
    launchme_executable(game_fps_tests TEST game_fps_history_and_overlay_bar SOURCES tests/game_fps_tests.cpp)
    launchme_executable(failure_cache_tests TEST persistent_artwork_failure_cache SOURCES tests/failure_cache_tests.cpp)
    launchme_executable(controller_models_tests TEST controller_picture_classification SOURCES tests/controller_models_tests.cpp)
    launchme_executable(profile_data_tests TEST play_journal_achievement_history_and_playtime SOURCES tests/profile_data_tests.cpp)
    launchme_executable(visibility_tests TEST steam_visibility SOURCES tests/visibility_tests.cpp)
    launchme_executable(ink_bounds_tests TEST logo_ink_bounds SOURCES tests/ink_bounds_tests.cpp)
    launchme_executable(shortcuts_tests TEST steam_nonsteam_shortcuts SOURCES tests/shortcuts_tests.cpp)
    launchme_executable(category_menu_tests TEST long_collection_menu SOURCES tests/category_menu_tests.cpp)
    launchme_executable(search_tests TEST live_library_search SOURCES tests/search_tests.cpp)
    launchme_executable(retry_policy_tests TEST artwork_retry_backoff SOURCES tests/retry_policy_tests.cpp)
    launchme_executable(lru_cache_tests TEST artwork_memory_lru SOURCES tests/lru_cache_tests.cpp)
    launchme_executable(background_work_tests TEST owned_background_workers SOURCES tests/background_work_tests.cpp)
    launchme_executable(screensaver_tests TEST screensaver_idle_playlist_and_input SOURCES tests/screensaver_tests.cpp)
    launchme_executable(input_gesture_tests TEST guide_start_hold_gestures SOURCES tests/input_gesture_tests.cpp)

    # Tests per area, as their sources are ported (same areas as above).
    # Processes:

    # Images:

    # Files and sockets:

    # Stores:
    launchme_executable(linux_stores_tests TEST linux_heroic_lutris_prism_libraries SOURCES tests/linux_stores_tests.cpp)
    launchme_executable(pc_platforms_tests TEST native_pc_platform_libraries SOURCES tests/pc_platforms_tests.cpp)
    launchme_executable(battlenet_tests TEST battlenet_local_installation_lifecycle SOURCES tests/battlenet_tests.cpp)
    launchme_executable(ubisoft_tests TEST ubisoft_connect_client_library SOURCES tests/ubisoft_tests.cpp)
    launchme_executable(store_import_tests TEST ea_battlenet_website_imports SOURCES tests/store_import_tests.cpp)
    launchme_executable(store_art_tests TEST store_own_artwork SOURCES tests/store_art_tests.cpp)
    launchme_executable(owned_games_tests TEST steam_app_names SOURCES tests/owned_games_tests.cpp)
    launchme_executable(steam_owned_tests TEST steam_client_owned_library SOURCES tests/steam_owned_tests.cpp)

    # Companion and updater:

    # System services:

    # Audio and video:

    # Controllers:

endif()
