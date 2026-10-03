cmake_minimum_required(VERSION 3.20)
project(calt_msg_host LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(calt_msg_host
  src/main.cpp
  src/softland_decide.cpp
  src/track_tab.cpp
  ../calt_enforcer/third_party/sqlite/sqlite3.c
)
target_include_directories(calt_msg_host PRIVATE
  src
  ../calt_enforcer/third_party/sqlite
)
target_compile_definitions(calt_msg_host PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN
  SQLITE_OMIT_LOAD_EXTENSION
)
if(MSVC)
  target_compile_options(calt_msg_host PRIVATE /W3 /utf-8)
else()
  target_compile_options(calt_msg_host PRIVATE -Wall)
endif()
target_link_libraries(calt_msg_host PRIVATE advapi32)