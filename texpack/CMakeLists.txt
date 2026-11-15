# SpeedBreaker's texture pack tools (-DSB_TEXPACK=ON): programs that read the
# player's installed disc files and build an HD texture pack from them. They
# are never linked into the game; the game only reads the finished pack.
add_library(texpack_core STATIC
    archive.cpp
    jdlz.cpp
    tpk.cpp
    decode.cpp
    extract.cpp
    encode.cpp
    thirdparty/bc7enc/bc7enc.cpp
)
target_include_directories(texpack_core PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/runtime)
target_compile_features(texpack_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(texpack_core PUBLIC Threads::Threads)

add_executable(texpack-extract extract_main.cpp)
target_link_libraries(texpack-extract PRIVATE texpack_core)
add_executable(texpack-encode encode_main.cpp)
target_link_libraries(texpack-encode PRIVATE texpack_core)
find_package(Vulkan REQUIRED)
add_executable(texpack-gpuinfo gpuinfo_main.cpp)
target_link_libraries(texpack-gpuinfo PRIVATE Vulkan::Vulkan)
