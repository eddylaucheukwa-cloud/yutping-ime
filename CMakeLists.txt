cmake_minimum_required(VERSION 3.20)
project(YutpingIME LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  message(FATAL_ERROR "TSF IME must be built with MSVC (cl.exe). Use the VS Developer prompt.")
endif()

add_library(YutpingIME SHARED
  src/Guids.cpp
  src/dllmain.cpp
  src/Register.cpp
  src/CantoneseIME.cpp
  src/GoogleInputAPI.cpp
  src/CandidateWindow.cpp
  src/UserSettings.cpp
  src/YutpingIME.def
)

target_link_libraries(YutpingIME PRIVATE
  ole32
  oleaut32
  uuid
  winhttp
  user32
  gdi32
  advapi32
)

# Static CRT: make DLL self-contained, no VCRUNTIME140.dll dependency
set_property(TARGET YutpingIME PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Unicode
target_compile_definitions(YutpingIME PRIVATE UNICODE _UNICODE)
# Force MSVC to treat source files as UTF-8 (fixes Chinese string literals)
target_compile_options(YutpingIME PRIVATE /utf-8)
set_target_properties(YutpingIME PROPERTIES OUTPUT_NAME "YutpingIME")

enable_testing()
add_executable(YutpingApiTest tests/ApiTest.cpp src/GoogleInputAPI.cpp)
target_include_directories(YutpingApiTest PRIVATE src)
target_link_libraries(YutpingApiTest PRIVATE winhttp advapi32)
target_compile_definitions(YutpingApiTest PRIVATE UNICODE _UNICODE)
target_compile_options(YutpingApiTest PRIVATE /utf-8)
set_property(TARGET YutpingApiTest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME ApiCandidates COMMAND YutpingApiTest)

add_executable(YutpingSettingsTest tests/SettingsTest.cpp src/UserSettings.cpp)
target_include_directories(YutpingSettingsTest PRIVATE src)
target_link_libraries(YutpingSettingsTest PRIVATE advapi32)
target_compile_definitions(YutpingSettingsTest PRIVATE UNICODE _UNICODE)
target_compile_options(YutpingSettingsTest PRIVATE /utf-8)
set_property(TARGET YutpingSettingsTest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME SelectionLearning COMMAND YutpingSettingsTest)

add_executable(YutpingInputTest tests/InputTest.cpp src/CantoneseIME.cpp src/UserSettings.cpp src/Guids.cpp)
target_include_directories(YutpingInputTest PRIVATE src)
target_link_libraries(YutpingInputTest PRIVATE ole32 uuid user32 advapi32)
target_compile_definitions(YutpingInputTest PRIVATE UNICODE _UNICODE)
target_compile_options(YutpingInputTest PRIVATE /utf-8)
set_property(TARGET YutpingInputTest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME InputBehavior COMMAND YutpingInputTest)

# Embed the DLL in a single native installer. Versioned install directories
# let Windows keep an older in-use DLL mapped while a new build is installed.
set(installer_rc "${CMAKE_CURRENT_BINARY_DIR}/YutpingSetup.rc")
add_custom_command(OUTPUT "${installer_rc}"
  COMMAND "${CMAKE_COMMAND}"
    "-DINPUT=$<TARGET_FILE:YutpingIME>"
    "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/src/Installer.manifest"
    "-DOUTPUT=${installer_rc}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateInstallerResource.cmake"
  DEPENDS YutpingIME src/Installer.manifest
  VERBATIM)
add_executable(YutpingSetup WIN32 src/Installer.cpp src/SettingsWindow.cpp src/UserSettings.cpp src/Guids.cpp "${installer_rc}")
target_include_directories(YutpingSetup PRIVATE src)
target_link_libraries(YutpingSetup PRIVATE ole32 uuid shell32 advapi32)
target_compile_definitions(YutpingSetup PRIVATE UNICODE _UNICODE)
target_compile_options(YutpingSetup PRIVATE /utf-8)
target_link_options(YutpingSetup PRIVATE /MANIFEST:NO)
set_property(TARGET YutpingSetup PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_custom_command(TARGET YutpingSetup POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:YutpingSetup>" "${CMAKE_CURRENT_SOURCE_DIR}/dist/YutpingSetup.exe"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:YutpingSetup>" "${CMAKE_CURRENT_SOURCE_DIR}/dist/YutpingUpdate.exe"
  VERBATIM)
