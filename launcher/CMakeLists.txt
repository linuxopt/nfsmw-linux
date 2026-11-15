# SpeedBreaker's desktop launcher (Linux): a window before the game for its
# settings and saves (docs/superpowers/specs/2026-11-15-gui-launcher-design.md).
# core/: the parts without Qt (rules, keys, save profiles, .sbsave archives,
# backups), always built on Linux so the tests can check them.
add_library(sb_launcher_core STATIC
    core/rules.cpp
    core/saves.cpp
    core/sbsave.cpp
    core/backups.cpp
)
target_include_directories(sb_launcher_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core ${CMAKE_SOURCE_DIR}/runtime)
target_compile_features(sb_launcher_core PUBLIC cxx_std_20)

# The Qt window (SB_LAUNCHER, top-level CMakeLists.txt): compiled with the
# game's own settings code (runtime/user/settings.cpp and what it needs), so
# its options, labels, ranges and settings.toml handling are the game's.
if(SB_LAUNCHER)
    set(RT ${CMAKE_SOURCE_DIR}/runtime)
    qt_add_library(sb_launcher_app STATIC
        app/launcher_window.cpp
        app/option_row.cpp
        app/controls_tab.cpp
        app/saves_tab.cpp
        ${RT}/user/settings.cpp
        ${RT}/user/paths.cpp
        ${RT}/platform/posix/file_io.cpp
    )
    set_target_properties(sb_launcher_app PROPERTIES AUTOMOC ON)
    target_include_directories(sb_launcher_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app ${RT})
    target_include_directories(sb_launcher_app SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/tools/XenonRecomp/thirdparty/tomlplusplus/include)
    target_compile_definitions(sb_launcher_app PUBLIC SB_VERSION="${CMAKE_PROJECT_VERSION}")
    target_link_libraries(sb_launcher_app PUBLIC sb_launcher_core Qt6::Widgets)
    qt_add_resources(sb_launcher_app "launcher_icon" PREFIX "/" BASE ${CMAKE_SOURCE_DIR}/scripts/macos
        FILES ${CMAKE_SOURCE_DIR}/scripts/macos/AppIcon.png)

    qt_add_executable(speedbreaker-launcher app/main.cpp)
    target_link_libraries(speedbreaker-launcher PRIVATE sb_launcher_app)
endif()
