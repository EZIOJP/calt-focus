cmake_minimum_required(VERSION 3.20)
project(calt_sync_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(calt_sync_host src/main.cpp)
target_compile_definitions(calt_sync_host PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

if(MSVC)
  target_compile_options(calt_sync_host PRIVATE /W3)
else()
  target_compile_options(calt_sync_host PRIVATE -Wall)
  target_link_options(calt_sync_host PRIVATE -static)
endif()

target_link_libraries(calt_sync_host PRIVATE ws2_32)
