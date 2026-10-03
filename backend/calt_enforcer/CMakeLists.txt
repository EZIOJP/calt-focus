cmake_minimum_required(VERSION 3.20)
project(calt_enforcer LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SQLite amalgamation — run scripts/desktop_tracker/fetch_sqlite.ps1 first
set(SQLITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
if(NOT EXISTS "${SQLITE_DIR}/sqlite3.c")
  message(FATAL_ERROR
    "Missing ${SQLITE_DIR}/sqlite3.c — run:\\n"
    "  powershell -File scripts/desktop_tracker/fetch_sqlite.ps1")
endif()

add_executable(calt_enforcer
  src/main.cpp
  src/policy_db.cpp
  src/kill.cpp
  src/owner_lock.cpp
  src/win_service.cpp
  src/foreground.cpp
  src/session_db.cpp
  src/status_writer.cpp
  src/focus_watchdog.cpp
  src/productivity_store.cpp
  src/softland_publish.cpp
  src/softland_tick.cpp
  src/cmd_gateway.cpp
  src/day_rollup.cpp
  src/day_loop.cpp
  src/plan_gate.cpp
  src/life_content.cpp
  src/device_block.cpp
  src/tracker_agent.cpp
  ${SQLITE_DIR}/sqlite3.c
)
target_include_directories(calt_enforcer PRIVATE src ${SQLITE_DIR})
target_compile_definitions(calt_enforcer PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN
  SQLITE_OMIT_LOAD_EXTENSION
)
if(MSVC)
  target_compile_options(calt_enforcer PRIVATE /W3)
else()
  # MinGW/Clang: wmain entry + Windows libs.
  # Prefer static libc++/libunwind so LocalSystem service starts without MinGW on PATH.
  target_compile_options(calt_enforcer PRIVATE -Wall -municode)
  target_link_options(calt_enforcer PRIVATE -municode -static)
endif()
target_link_libraries(calt_enforcer PRIVATE advapi32 user32 winhttp wtsapi32 userenv)
