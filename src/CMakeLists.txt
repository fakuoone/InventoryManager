cmake_minimum_required(VERSION 3.20)

project(InventoryManager VERSION 1.0 LANGUAGES CXX)

# --- Options ---
option(USE_STATIC "Build with static libraries" ON)

# --- Compiler ---
set(CMAKE_CXX_COMPILER C:/msys64/mingw64/bin/g++.exe)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Linker flags for static builds ---
if(USE_STATIC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
endif()

# --- Vcpkg ---
set(VCPKG_BASE "B:/vcpkg/vcpkg/installed")
set(CMAKE_TOOLCHAIN_FILE "B:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake")

# --- Output directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../lib)

# --- Source files ---
set(SOURCES
    main.cpp
    threadPool.cpp
    changeTracker.cpp
)

set(HEADERS
    ${CMAKE_SOURCE_DIR}/include/dbInterface.hpp
)

# --- Executable ---
add_executable(InventoryManager ${SOURCES} ${HEADERS})

# --- Determine paths based on static/dynamic build ---
if(USE_STATIC)
    set(VCPKG_TRIPLET_DIR "${VCPKG_BASE}/x64-mingw-static")
    set(LIBPQXX_LIB "${VCPKG_TRIPLET_DIR}/lib/libpqxx.a")
    set(LIBPQ_LIB "${VCPKG_TRIPLET_DIR}/lib/libpq.a")
else()
    set(VCPKG_TRIPLET_DIR "${VCPKG_BASE}/x64-mingw-dynamic")
    set(LIBPQXX_LIB "${VCPKG_TRIPLET_DIR}/lib/libpqxx.dll.a")
    set(LIBPQ_LIB "${VCPKG_TRIPLET_DIR}/lib/libpq.dll.a")
endif()

# --- Include directories ---
target_include_directories(InventoryManager PRIVATE
    "${VCPKG_TRIPLET_DIR}/include"
)

# --- Libraries ---
target_link_libraries(InventoryManager PRIVATE
    "${LIBPQXX_LIB}"
    "${LIBPQ_LIB}"
    ws2_32
)
