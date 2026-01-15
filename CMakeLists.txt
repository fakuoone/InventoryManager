cmake_minimum_required(VERSION 3.21)

project(InventoryManager VERSION 1.0 LANGUAGES CXX)

# --- Compiler ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Options: static-ish MinGW builds ---


# --- Output directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Source files ---
set(SOURCES
    src/main.cpp
    src/threadPool.cpp
    src/changeTracker.cpp
    src/imGuiDX11Context.cpp
)

# --- Executable ---
add_executable(InventoryManager ${SOURCES})
set_target_properties(InventoryManager PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# --- IMGUI ---
set(IMGUI_DIR "" CACHE PATH "Path to Dear ImGui source directory")

file(GLOB IMGUI_GLOB
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imconfig.h
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imstb_rectpack.h
    ${IMGUI_DIR}/imstb_textedit.h
    ${IMGUI_DIR}/imstb_truetype.h
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.h
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.h
)

set(IMGUI_LIBRARY_TYPE "STATIC" CACHE STRING "ImGui library type")
set_property(CACHE IMGUI_LIBRARY_TYPE PROPERTY STRINGS STATIC SHARED)

add_library(imgui ${IMGUI_LIBRARY_TYPE} ${IMGUI_GLOB})
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

set_target_properties(imgui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(imgui PUBLIC
    d3d11
    dxgi
    d3dcompiler
    imm32
    user32
    gdi32
    dwmapi
)



# --- Includes ---
target_include_directories(InventoryManager PRIVATE
    ${PROJECT_SOURCE_DIR}/src/include
)

# --- Dependencies ---
find_package(libpqxx CONFIG REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(InventoryManager PRIVATE
    libpqxx::pqxx
    PostgreSQL::PostgreSQL
    nlohmann_json::nlohmann_json
    imgui
    ws2_32
)
