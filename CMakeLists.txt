cmake_minimum_required(VERSION 3.16)
project(potato_launcher C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- miniz (vendored) -------------------------------------------------------
add_library(miniz STATIC
    vendor/miniz/miniz.c
    vendor/miniz/miniz_tdef.c
    vendor/miniz/miniz_tinfl.c
    vendor/miniz/miniz_zip.c
)
target_include_directories(miniz PUBLIC vendor/miniz)
target_compile_definitions(miniz PUBLIC _FILE_OFFSET_BITS=64 _LARGEFILE64_SOURCE)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- the launcher -----------------------------------------------------------
add_executable(potato-launcher
    src/main.cpp
    src/platform.cpp
    src/manifest.cpp
    src/command.cpp
    src/process.cpp
    src/zip.cpp
    src/launcher.cpp
    src/auth/crypto.cpp
    src/auth/http.cpp
    src/auth/accountstore.cpp
    src/auth/yggdrasil.cpp
    src/auth/offline.cpp
    src/auth/authserver.cpp
)
target_include_directories(potato-launcher PRIVATE
    src
    vendor
    vendor/nlohmann
)
target_link_libraries(potato-launcher PRIVATE miniz)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(potato-launcher PRIVATE -Wall -Wextra)
    find_package(Threads REQUIRED)
    target_link_libraries(potato-launcher PRIVATE Threads::Threads)
endif()

install(TARGETS potato-launcher RUNTIME DESTINATION bin)
