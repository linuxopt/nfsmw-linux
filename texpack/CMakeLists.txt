# SpeedBreaker's texture pack tools (-DSB_TEXPACK=ON): programs that read the
# player's installed disc files and build an HD texture pack from them. They
# are never linked into the game; the game only reads the finished pack.
add_library(texpack_core STATIC
    archive.cpp
)
target_include_directories(texpack_core PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/runtime)
target_compile_features(texpack_core PUBLIC cxx_std_20)
