# SpeedBreaker's desktop launcher (Linux): a window before the game for its
# settings and saves (docs/superpowers/specs/2026-11-15-gui-launcher-design.md).
# core/: the parts without Qt (rules, keys, save profiles, .sbsave archives,
# backups), always built on Linux so the tests can check them.
add_library(sb_launcher_core STATIC
    core/rules.cpp
    core/saves.cpp
    core/sbsave.cpp
)
target_include_directories(sb_launcher_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core ${CMAKE_SOURCE_DIR}/runtime)
target_compile_features(sb_launcher_core PUBLIC cxx_std_20)
