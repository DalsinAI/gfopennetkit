# gfopennetkit host build: the library and its unit tests.
# The Amiga build is build-amiga.sh. MIT licence, (c) 2026 AmiCradle Team.
cmake_minimum_required(VERSION 3.10)
project(gfopennetkit C)
set(CMAKE_C_STANDARD 99)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Werror)
endif()

add_library(gfopennetkit STATIC
  src/nk_crc32.c src/nk_util.c src/nk_line.c src/nk_conn.c
  src/nk_socks5.c src/nk_tls.c src/nk_journal.c src/nk_posix.c
  src/nk_textview.c)
target_include_directories(gfopennetkit PUBLIC include)

# Tests only when this is the top project (GFOpenNews and GFOpenIRC take the
# library with add_subdirectory and build only it).
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  return()
endif()

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
foreach(t crc32 line conn socks5 tls journal textview util)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} gfopennetkit)
  target_include_directories(test_${t} PRIVATE tests)
endforeach()
add_test(NAME crc32 COMMAND test_crc32)
add_test(NAME line COMMAND test_line)
add_test(NAME conn COMMAND test_conn)
add_test(NAME socks5 COMMAND test_socks5)
add_test(NAME tls COMMAND test_tls)
add_test(NAME journal COMMAND test_journal ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME textview COMMAND test_textview)
add_test(NAME util COMMAND test_util)

add_executable(test_live tests/test_live.c)
target_link_libraries(test_live gfopennetkit)
target_include_directories(test_live PRIVATE tests)
if(Python3_FOUND)
  add_test(NAME live_fake_servers
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fakes/fakeserv.py
            --run nntp,irc,socks5 -- $<TARGET_FILE:test_live>)
endif()
