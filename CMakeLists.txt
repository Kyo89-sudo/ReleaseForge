cmake_minimum_required(VERSION 3.20)
project(ReleaseForge VERSION 0.2.0 LANGUAGES CXX RC)

# App Win32 nativa (C++23). Nessuna dipendenza esterna: WinHTTP, BCrypt, GDI+, Common Controls.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # /MT: nessun VC++ Redistributable
endif()

set(CORE_SOURCES
  src/core/jinja.cpp src/core/json.cpp src/core/naming.cpp src/core/strutil.cpp src/core/torrent.cpp src/core/torrent_v2.cpp)
set(WIN_SOURCES
  src/win/bitrate.cpp src/win/http.cpp src/win/mediainfo.cpp src/win/online.cpp src/win/pipeline.cpp
  src/win/process.cpp src/win/settings.cpp src/win/tools.cpp src/win/trackerlists.cpp src/win/winutil.cpp)
set(APP_SOURCES src/app/main.cpp src/app/dialogs.cpp src/app/ReleaseForge.rc)

add_executable(ReleaseForge WIN32 ${CORE_SOURCES} ${WIN_SOURCES} ${APP_SOURCES})
target_compile_definitions(ReleaseForge PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(ReleaseForge PRIVATE comctl32 winhttp bcrypt shlwapi shell32 ole32 uuid gdiplus uxtheme)

if(MSVC)
  target_compile_options(ReleaseForge PRIVATE /W4 /utf-8 /permissive- /EHsc)
  target_link_options(ReleaseForge PRIVATE /MANIFEST:NO)   # il manifest è nel file .rc
else()
  target_compile_options(ReleaseForge PRIVATE -Wall -Wextra -municode)
  target_link_options(ReleaseForge PRIVATE -municode -static -s)
endif()

# Test del core (portabili): cmake --build . --target core_tests && ctest
add_executable(core_tests tests/core_tests.cpp ${CORE_SOURCES})
target_include_directories(core_tests PRIVATE src)
if(MSVC)
  target_compile_options(core_tests PRIVATE /utf-8 /EHsc)
endif()
enable_testing()
add_test(NAME core_tests COMMAND core_tests)

add_executable(torrent_tests tests/torrent_tests.cpp ${CORE_SOURCES})
target_include_directories(torrent_tests PRIVATE src)
if(MSVC)
  target_compile_options(torrent_tests PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME torrent_tests COMMAND torrent_tests)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME torrent_reference COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/torrent_reference.py $<TARGET_FILE:torrent_tests>)
endif()
