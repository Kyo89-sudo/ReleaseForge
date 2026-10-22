cmake_minimum_required(VERSION 3.20)
project(ReleaseForgeInstaller LANGUAGES CXX RC)

# Installer nativo di ReleaseForge (nessun Inno Setup). Uso tipico: build-installer.bat.
# PAYLOAD_DIR deve contenere il ReleaseForge.exe appena compilato (build.bat lo copia in dist).
set(PAYLOAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dist" CACHE PATH "Cartella con ReleaseForge.exe da includere nel setup")

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../src/resource.h" _ver REGEX "^#define RF_VERSION_STR ")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" RF_VERSION "${_ver}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(INSTALLER_LIBS comctl32 shell32 ole32 uuid advapi32)

function(rf_installer_target name)
  target_compile_definitions(${name} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(${name} PRIVATE ${INSTALLER_LIBS})
  if(MSVC)
    target_compile_options(${name} PRIVATE /W4 /utf-8 /permissive- /EHsc)
    target_link_options(${name} PRIVATE /MANIFEST:NO)
  else()
    target_compile_options(${name} PRIVATE -Wall -Wextra -municode)
    target_link_options(${name} PRIVATE -municode -static -s)
  endif()
endfunction()

# 1) uninstall.exe: stesso sorgente, senza payload. Uscita fissa in payload/ (anche con Visual Studio).
add_executable(rf_uninstall WIN32 setup.cpp installer.rc)
rf_installer_target(rf_uninstall)
set_target_properties(rf_uninstall PROPERTIES OUTPUT_NAME uninstall
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/payload$<0:>")

# 2) setup: incorpora ReleaseForge.exe, uninstall.exe e i documenti.
add_executable(rf_setup WIN32 setup.cpp installer.rc)
rf_installer_target(rf_setup)
target_compile_definitions(rf_setup PRIVATE RF_WITH_PAYLOAD)
target_include_directories(rf_setup PRIVATE "${PAYLOAD_DIR}" "${CMAKE_BINARY_DIR}/payload")
add_dependencies(rf_setup rf_uninstall)
set_target_properties(rf_setup PROPERTIES OUTPUT_NAME "ReleaseForge-${RF_VERSION}-win64-setup")
