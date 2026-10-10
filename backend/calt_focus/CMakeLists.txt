cmake_minimum_required(VERSION 3.20)
project(calt_focus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2")
set(WEBVIEW2_INCLUDE "${WEBVIEW2_DIR}/include")
set(WEBVIEW2_DLL "${WEBVIEW2_DIR}/x64/WebView2Loader.dll")

if(NOT EXISTS "${WEBVIEW2_INCLUDE}/WebView2.h")
  message(FATAL_ERROR
    "Missing WebView2.h — run:\\n"
    "  powershell -File scripts/build/fetch_webview2.ps1")
endif()
if(NOT EXISTS "${WEBVIEW2_DLL}")
  message(FATAL_ERROR
    "Missing WebView2Loader.dll — run:\\n"
    "  powershell -File scripts/build/fetch_webview2.ps1")
endif()

add_executable(calt_focus WIN32
  src/main.cpp
  src/app.cpp
  src/webview_host.cpp
  src/tray_icon.cpp
  src/paths.cpp
  src/status_badge.cpp
  src/enforcer_cmd.cpp
  src/wearable_listen.cpp
)

target_include_directories(calt_focus PRIVATE
  src
  ${WEBVIEW2_INCLUDE}
)

target_compile_definitions(calt_focus PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

if(MSVC)
  target_compile_options(calt_focus PRIVATE /W3 /EHsc)
else()
  target_compile_options(calt_focus PRIVATE -Wall -fms-extensions)
  target_link_options(calt_focus PRIVATE -municode)
endif()

target_link_libraries(calt_focus PRIVATE
  ole32
  oleaut32
  user32
  gdi32
  shell32
  advapi32
  comctl32
  shlwapi
  version
  ws2_32
  dwmapi
)

# Copy loader DLL next to the exe after build
add_custom_command(TARGET calt_focus POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${WEBVIEW2_DLL}"
    "$<TARGET_FILE_DIR:calt_focus>/WebView2Loader.dll"
  COMMENT "Copy WebView2Loader.dll beside calt_focus.exe"
)
