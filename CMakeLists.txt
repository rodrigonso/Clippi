cmake_minimum_required(VERSION 3.20)
project(Clippi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Convention: no C++ standard library, no exceptions, no RTTI.
if(MSVC)
  string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  add_compile_options(/GR- /EHs-c- /W4 /permissive- /Zc:__cplusplus)
  add_compile_definitions(
    _HAS_EXCEPTIONS=0
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---- base ----------------------------------------------------------------
if(WIN32)
  set(CLIPPI_THREAD_SRC src/base/thread_win.cpp)
else()
  set(CLIPPI_THREAD_SRC src/base/thread_posix.cpp)
endif()
add_library(clippi_base STATIC
  src/base/arena.cpp
  src/base/config_file.cpp
  src/base/process_file_lock.cpp
  src/base/sysinfo.cpp
  ${CLIPPI_THREAD_SRC})
target_include_directories(clippi_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(WIN32)
  target_link_libraries(clippi_base PUBLIC psapi)
else()
  target_link_libraries(clippi_base PUBLIC pthread)
endif()

# ---- json ----------------------------------------------------------------
add_library(clippi_json STATIC
  src/json/json.cpp)
target_link_libraries(clippi_json PUBLIC clippi_base)

# ---- crypto --------------------------------------------------------------
add_library(clippi_crypto STATIC
  src/crypto/sha256.cpp
  src/crypto/random.cpp)
target_link_libraries(clippi_crypto PUBLIC clippi_base)
if(WIN32)
  target_link_libraries(clippi_crypto PUBLIC bcrypt)
endif()

# ---- durable sessions ---------------------------------------------------
add_library(clippi_session STATIC
  src/session/id.cpp
  src/session/entry.cpp
  src/session/reducer.cpp
  src/session/memory_repository.cpp
  src/session/jsonl_repository.cpp)
target_link_libraries(clippi_session PUBLIC clippi_base clippi_json clippi_crypto)

# ---- agent ---------------------------------------------------------------
add_library(clippi_agent STATIC
  src/agent/harness.cpp
  src/agent/driver.cpp
  src/agent/primitives.cpp
  src/llm/retry.cpp)
target_link_libraries(clippi_agent PUBLIC clippi_base clippi_session)

# ---- libcurl (vendored via FetchContent) ---------------------------------
include(FetchContent)
set(CLIPPI_FETCHCONTENT_CACHE_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/out/fetchcontent"
  CACHE PATH "Persistent download cache for vendored dependencies")
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
if(WIN32)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
elseif(APPLE)
  set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
else()
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(curl
  URL https://curl.se/download/curl-8.11.1.tar.xz
  URL_HASH SHA256=c7ca7db48b0909743eaef34250da02c19bc61d4f1dcedd6603f109409536ab56
  DOWNLOAD_DIR "${CLIPPI_FETCHCONTENT_CACHE_ROOT}/downloads"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(curl)

add_library(clippi_http STATIC src/net/http_curl.cpp)
target_link_libraries(clippi_http PUBLIC clippi_base CURL::libcurl)

# ---- loopback (OAuth callback server + browser opener) -------------------
add_library(clippi_loopback STATIC
  src/net/loopback.cpp
  src/net/browser.cpp)
target_link_libraries(clippi_loopback PUBLIC clippi_base)
if(WIN32)
  target_link_libraries(clippi_loopback PUBLIC ws2_32 shell32)
endif()

# ---- term (full-screen TUI) ----------------------------------------------
if(WIN32)
  set(CLIPPI_TERM_PLATFORM src/term/term_win.cpp)
else()
  set(CLIPPI_TERM_PLATFORM src/term/term_posix.cpp)
endif()
add_library(clippi_term STATIC
  src/term/term.cpp
  src/term/keys.cpp
  src/term/cell.cpp
  src/term/screen.cpp
  src/term/markdown.cpp
  src/term/sanitize.cpp
  src/term/stream_reveal.cpp
  src/term/linemodel.cpp
  src/term/toolview.cpp
  src/term/lineedit.cpp
  src/term/tui.cpp
  ${CLIPPI_TERM_PLATFORM})
target_link_libraries(clippi_term PUBLIC clippi_base clippi_agent clippi_json)

# ---- llm providers -------------------------------------------------------
add_library(clippi_proto STATIC src/llm/anthropic_proto.cpp)
target_link_libraries(clippi_proto PUBLIC clippi_json)

add_library(clippi_anthropic STATIC src/llm/anthropic.cpp)
target_link_libraries(clippi_anthropic PUBLIC clippi_proto clippi_http)

# ---- auth (api keys + OAuth) ---------------------------------------------
add_library(clippi_auth STATIC
  src/auth/store.cpp
  src/auth/oauth.cpp
  src/auth/resolve.cpp)
target_link_libraries(clippi_auth PUBLIC clippi_json clippi_crypto clippi_http clippi_loopback)

# ---- tools ---------------------------------------------------------------
add_library(clippi_tools STATIC
  src/tools/exec.cpp
  src/tools/fs.cpp
  src/tools/truncate.cpp
  src/tools/glob.cpp
  src/tools/regex.cpp
  src/tools/diff.cpp
  src/tools/filelock.cpp
  src/tools/read.cpp
  src/tools/write.cpp
  src/tools/edit.cpp
  src/tools/ls.cpp
  src/tools/find.cpp
  src/tools/grep.cpp
  src/tools/bash.cpp)
target_link_libraries(clippi_tools PUBLIC clippi_json clippi_agent)

# ---- app support (settings, provider registry, interactive commands) ------
add_library(clippi_app STATIC
  src/app/command.cpp
  src/app/plain_select.cpp
  src/app/provider_registry.cpp
  src/app/settings.cpp)
target_link_libraries(clippi_app PUBLIC clippi_anthropic clippi_auth clippi_term)

# ---- executables ---------------------------------------------------------
add_executable(clippi src/app/main.cpp)
target_link_libraries(clippi PRIVATE clippi_app clippi_anthropic clippi_tools clippi_agent clippi_auth clippi_term)

add_executable(clippi_http_smoke EXCLUDE_FROM_ALL src/app/http_smoke.cpp)
target_link_libraries(clippi_http_smoke PRIVATE clippi_http)

# ---- tests ---------------------------------------------------------------
enable_testing()
add_executable(clippi_tests EXCLUDE_FROM_ALL
  tests/test_main.cpp
  tests/test_base.cpp
  tests/test_types.cpp
  tests/test_json.cpp
  tests/test_session.cpp
  tests/test_harness.cpp
  tests/test_anthropic.cpp
  tests/test_tools.cpp
  tests/test_infra.cpp
  tests/test_crypto.cpp
  tests/test_auth.cpp
  tests/test_command.cpp
  tests/test_loopback.cpp
  tests/test_provider_registry.cpp
  tests/test_settings.cpp
  tests/test_thread.cpp
  tests/test_keys.cpp
  tests/test_screen.cpp
  tests/test_markdown.cpp
  tests/test_sanitize.cpp
  tests/test_stream_reveal.cpp
  tests/test_linemodel.cpp
  tests/test_lineedit.cpp
  tests/test_tui.cpp)
target_link_libraries(clippi_tests PRIVATE clippi_app clippi_proto clippi_agent clippi_session clippi_tools clippi_crypto clippi_auth clippi_loopback clippi_term)
add_test(NAME unit COMMAND clippi_tests)
