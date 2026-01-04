
# cmake DCMAKE_BUILD_TYPE:STRING=Debug -DBUILD_SHARED_LIBS:BOOL=OFF -DCMAKE_TOOLCHAIN_FILE="B:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=C:\msys64\ucrt64\bin\gcc.exe -DCMAKE_CXX_COMPILER:FILEPATH=C:\msys64\ucrt64\bin\g++.exe --no-warn-unused-cli  -SB:/Programmieren/C/CerboEnergy/src  -BB:/Programmieren/C/CerboEnergy/build -G "MinGW Makefiles"

# cmake DCMAKE_BUILD_TYPE:STRING=Debug -DCMAKE_TOOLCHAIN_FILE="B:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=C:\msys64\ucrt64\bin\gcc.exe -DCMAKE_CXX_COMPILER:FILEPATH=C:\msys64\ucrt64\bin\g++.exe --no-warn-unused-cli  -SB:/Programmieren/C/CerboEnergy/src  -BB:/Programmieren/C/CerboEnergy/build -G "MinGW Makefiles"

# cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug

# vcpkg install curl:x64-mingw-static --host-triplet=x64-mingw-static

cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_COMPILER "C:/msys64/ucrt64/bin/gcc.exe" )
set(CMAKE_CXX_COMPILER "C:/msys64/ucrt64/bin/g++.exe" )

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(LINKTYPE "STATIC")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Werror -Wall -Og")
# Set flags for static linking
IF(LINKTYPE STREQUAL STATIC)
    link_directories("B:/vcpkg/vcpkg/installed/x64-mingw-static/lib")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -static-libgcc -static-libstdc++")
    set(CMAKE_CXX_STANDARD_LIBRARIES "-static-libgcc -static-libstdc++ ${CMAKE_CXX_STANDARD_LIBRARIES}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
    set(BUILD_SHARED_LIBS OFF)
else()
    link_directories("B:/vcpkg/vcpkg/installed/x64-mingw-dynamic/lib")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--whole-archive -lwinpthread -Wl,--no-whole-archive")
    set(BUILD_SHARED_LIBS OFF)
endif()

set(CMAKE_TOOLCHAIN_FILE "B:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake")
project(InventoryManager VERSION 1.0)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)

set(SOURCES main.cpp ${CMAKE_SOURCE_DIR}/threadPool.cpp ${CMAKE_SOURCE_DIR}/changeTracker.cpp)

set(HEADERS ${CMAKE_SOURCE_DIR}/include/dbInterface.hpp)

add_executable(InventoryManager ${SOURCES} ${HEADERS})
target_include_directories(InventoryManager PRIVATE ${CMAKE_SOURCE_DIR}/include "B:/vcpkg/vcpkg/installed/x64-mingw-static/include")
target_link_libraries(InventoryManager PRIVATE pqxx pq ws2_32)

# configure_file(mainConfig.h.in ${CMAKE_BINARY_DIR}/../src/mainConfig.h)

# find_package(nlohmann_json CONFIG REQUIRED)


