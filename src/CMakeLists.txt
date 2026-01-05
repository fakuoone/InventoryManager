cmake_minimum_required(VERSION 3.20)

project(InventoryManager VERSION 1.0 LANGUAGES CXX)

# --- Options ---
option(USE_STATIC "Build with static libraries" ON)

# --- Compiler ---
set(CMAKE_CXX_COMPILER C:/msys64/mingw64/bin/g++.exe)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Linker flags  ---
SET(LINK_TYPE "DYNAMIC") 
if(USE_STATIC)
    set(LINK_TYPE "STATIC")
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
    userInterface/dx11Context.cpp
)

# --- Executable ---
add_executable(InventoryManager ${SOURCES} ${HEADERS})

# --- IMGUI ---
set(IMGUI_PATH  "B:/Programmieren/C/Libs/imgui")
file(GLOB IMGUI_GLOB
    ${IMGUI_PATH}/imgui.h
    ${IMGUI_PATH}/imgui.cpp
    ${IMGUI_PATH}/imconfig.h
    ${IMGUI_PATH}/imgui_demo.cpp
    ${IMGUI_PATH}/imgui_draw.cpp
    ${IMGUI_PATH}/imgui_internal.h
    ${IMGUI_PATH}/imstb_rectpack.h
    ${IMGUI_PATH}/imstb_textedit.h
    ${IMGUI_PATH}/imstb_truetype.h
    ${IMGUI_PATH}/imgui_tables.cpp
    ${IMGUI_PATH}/imgui_widgets.cpp

    ${IMGUI_PATH}/backends/imgui_impl_win32.h
    ${IMGUI_PATH}/backends/imgui_impl_win32.cpp
    ${IMGUI_PATH}/backends/imgui_impl_dx11.cpp
    ${IMGUI_PATH}/backends/imgui_impl_dx11.h)

add_library(imgui ${LINKTYPE} ${IMGUI_GLOB})
target_compile_definitions(imgui PRIVATE IMGUI_EXPORTS)
target_include_directories(imgui PUBLIC ${IMGUI_PATH})
target_include_directories(imgui PUBLIC ${IMGUI_PATH}/backends)

target_link_libraries(imgui PUBLIC
    d3d11
    dxgi
    d3dcompiler
    imm32
    user32
    gdi32
    dwmapi
)


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
    "${PROJECT_SOURCE_DIR}/include"
)

# --- Libraries ---
target_link_libraries(InventoryManager PRIVATE
    "${LIBPQXX_LIB}"
    "${LIBPQ_LIB}"
    ws2_32
    imgui
)
