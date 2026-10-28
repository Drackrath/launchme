# Linux: Wine/Proton/umu runner, compatibility tool versions, prefixes and their tests.
# Included at the end of cmake/Linux.cmake. The backend lives in launchme_core (the app and the
# tests link it); docs/LINUX_WINE.md describes it.

# libarchive extracts the downloaded tools (.tar.gz, .tar.xz, .tar.zst).
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)
target_sources(launchme_core PRIVATE
    src/library/compat_tools.cpp
    src/library/launch_options_store.cpp
    src/model/launch_options_catalog.cpp
    src/platform/wine_runner.cpp)
target_link_libraries(launchme_core PUBLIC PkgConfig::LIBARCHIVE)

if(BUILD_TESTING)
    launchme_executable(wine_runner_tests TEST wine_runner_commands_and_options SOURCES tests/wine_runner_tests.cpp)
    launchme_executable(compat_tools_tests TEST compat_tools_discovery_releases_and_archives SOURCES tests/compat_tools_tests.cpp)
    # Not in CTest: starts real Wine/Proton programs (tests/wine_smoke_test.cpp).
    launchme_executable(wine_smoke_test SOURCES tests/wine_smoke_test.cpp)
endif()
