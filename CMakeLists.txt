cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CLIPPI_VERSION LIMIT_COUNT 1)
project(Clippi VERSION "${CLIPPI_VERSION}" LANGUAGES C CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)

option(CLIPPI_BUILD_CLI "Build the CLI and its networking dependencies" ON)
option(CLIPPI_BUILD_TESTS "Build the offline-by-default test runner" OFF)
option(CLIPPI_BUILD_EXAMPLES "Build the offline core example" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
foreach(kind RUNTIME ARCHIVE LIBRARY)
  # Keep paths identical for single- and multi-configuration generators.
  set(CMAKE_${kind}_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<0:>")
endforeach()

if(MSVC)
  string(REGEX REPLACE " /W[0-4]| /GR| /EH[a-z-]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else()
  # C++ objects use only the C runtime; do not add a C++ runtime at link time.
  list(REMOVE_ITEM CMAKE_CXX_IMPLICIT_LINK_LIBRARIES stdc++ c++ c++abi)
endif()

function(clippi_target target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /GR- /EHs-c- /W4 /permissive- /Zc:__cplusplus /utf-8)
    target_compile_definitions(${target} PRIVATE
      _HAS_EXCEPTIONS=0 NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE
      -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra)
    set_target_properties(${target} PROPERTIES LINKER_LANGUAGE C)
  endif()
endfunction()

add_library(clippi_core STATIC
  src/base/arena.cpp
  src/base/config_file.cpp
  src/base/file_io.cpp
  src/base/process_file_lock.cpp
  src/base/sysinfo.cpp
  src/json/json.cpp
  src/json/schema.cpp
  src/tools/fs.cpp
  src/crypto/random.cpp
  src/session/id.cpp
  src/session/entry.cpp
  src/session/metadata.cpp
  src/session/reducer.cpp
  src/session/memory_repository.cpp
  src/session/jsonl_repository.cpp
  src/llm/retry.cpp
  src/agent/harness.cpp
  src/agent/driver.cpp
  src/agent/primitives.cpp
  src/agent/permissions.cpp
  src/agent/compaction.cpp
  src/agent/event_json.cpp)
clippi_target(clippi_core)
set_target_properties(clippi_core PROPERTIES DEBUG_POSTFIX d)
target_compile_features(clippi_core PUBLIC cxx_std_17)
target_include_directories(clippi_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(WIN32)
  target_sources(clippi_core PRIVATE src/base/thread_win.cpp)
  target_link_libraries(clippi_core PUBLIC psapi bcrypt)
else()
  find_package(Threads REQUIRED)
  target_sources(clippi_core PRIVATE src/base/thread_posix.cpp)
  target_link_libraries(clippi_core PUBLIC Threads::Threads m)
endif()

if(CLIPPI_BUILD_CLI)
  include(cmake/curl.cmake)
  # Compile CLI implementation once, shared by the executable and its tests.
  add_library(clippi_cli OBJECT
    src/net/http_curl.cpp
    src/term/term.cpp
    src/term/keys.cpp
    src/term/cell.cpp
    src/term/screen.cpp
    src/term/sanitize.cpp
    src/ui/lineedit.cpp
    src/ui/ui.cpp
    src/ui/layout.cpp
    src/ui/canvas.cpp
    src/ui/event.cpp
    src/ui/widgets.cpp
    src/tui/markdown.cpp
    src/tui/modal.cpp
    src/tui/perf.cpp
    src/tui/selection.cpp
    src/tui/session.cpp
    src/tui/status.cpp
    src/tui/stream_reveal.cpp
    src/tui/linemodel.cpp
    src/tui/toolview.cpp
    src/tui/tui.cpp
    src/llm/responses_proto.cpp
    src/llm/responses.cpp
    src/auth/store.cpp
    src/auth/resolve.cpp
    src/tools/exec.cpp
    src/tools/tools_manager.cpp
    src/tools/tool_select.cpp
    src/tools/truncate.cpp
    src/tools/glob.cpp
    src/tools/diff.cpp
    src/tools/filelock.cpp
    src/tools/read.cpp
    src/tools/write.cpp
    src/tools/edit.cpp
    src/tools/ls.cpp
    src/tools/find.cpp
    src/tools/grep.cpp
    src/tools/ask_user.cpp
    src/tools/shell.cpp
    src/app/auth_flow.cpp
    src/app/command.cpp
    src/app/commands.cpp
    src/app/config_paths.cpp
    src/app/event_pump.cpp
    src/app/frontmatter.cpp
    src/app/hook_config.cpp
    src/app/hook_gate.cpp
    src/app/hook_runner.cpp
    src/app/plain_select.cpp
    src/app/project_trust.cpp
    src/app/prompt_context.cpp
    src/app/prompt_templates.cpp
    src/app/provider_registry.cpp
    src/app/runtime.cpp
    src/app/session_switch.cpp
    src/app/settings.cpp
    src/app/skills.cpp
    src/app/tui_bridge.cpp)
  clippi_target(clippi_cli)
  target_link_libraries(clippi_cli PUBLIC clippi_core CURL::libcurl)
  if(WIN32)
    target_sources(clippi_cli PRIVATE src/term/term_win.cpp)
    target_link_libraries(clippi_cli PUBLIC ws2_32 crypt32 advapi32)
  else()
    target_sources(clippi_cli PRIVATE src/term/term_posix.cpp)
  endif()
  add_executable(clippi src/app/main.cpp)
  clippi_target(clippi)
  target_link_libraries(clippi PRIVATE clippi_cli)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT clippi)
  set_target_properties(clippi PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  install(TARGETS clippi RUNTIME DESTINATION bin COMPONENT runtime)
  install(FILES "${curl_SOURCE_DIR}/COPYING"
    DESTINATION share/clippi/licenses/curl COMPONENT runtime)
endif()

if(CLIPPI_BUILD_TESTS)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tests/suites.txt)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/tests/suites.txt" suites REGEX "^[^#]")
  set(test_sources tests/test_main.cpp tests/benchmark_agent.cpp)
  set(registration "")
  foreach(row IN LISTS suites)
    string(REPLACE "|" ";" fields "${row}")
    list(LENGTH fields count)
    if(NOT count EQUAL 6)
      message(FATAL_ERROR "Invalid test suite row: ${row}")
    endif()
    list(GET fields 0 group)
    if(NOT group MATCHES "^(core|cli)$")
      message(FATAL_ERROR "Invalid test suite group: ${group}")
    endif()
    if(group STREQUAL "cli" AND NOT CLIPPI_BUILD_CLI)
      continue()
    endif()
    list(GET fields 1 name)
    list(GET fields 2 function)
    list(GET fields 3 source)
    list(GET fields 4 tag)
    list(GET fields 5 description)
    string(REPLACE "\\" "\\\\" description "${description}")
    string(REPLACE "\"" "\\\"" description "${description}")
    string(APPEND registration
      "CLIPPI_TEST_SUITE(\"${name}\", ${function}, ${tag}, \"${description}\")\n")
    list(APPEND test_sources "tests/${source}.cpp")
  endforeach()
  list(REMOVE_DUPLICATES test_sources)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/test_suites.inc"
    CONTENT "${registration}")
  add_executable(clippi_tests ${test_sources})
  clippi_target(clippi_tests)
  target_include_directories(clippi_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  if(CLIPPI_BUILD_CLI)
    target_link_libraries(clippi_tests PRIVATE clippi_cli)
  else()
    target_link_libraries(clippi_tests PRIVATE clippi_core)
  endif()
  enable_testing()
  add_test(NAME offline COMMAND clippi_tests)
endif()

if(CLIPPI_BUILD_EXAMPLES)
  add_executable(clippi_core_example examples/core/main.cpp)
  clippi_target(clippi_core_example)
  target_link_libraries(clippi_core_example PRIVATE clippi_core)
  set_target_properties(clippi_core_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/examples/core/$<0:>")
endif()

install(TARGETS clippi_core ARCHIVE DESTINATION lib COMPONENT sdk)
install(DIRECTORY src/base src/json src/crypto src/session src/agent
  DESTINATION include/clippi COMPONENT sdk FILES_MATCHING PATTERN "*.h")
install(FILES src/llm/types.h src/llm/retry.h DESTINATION include/clippi/llm COMPONENT sdk)
install(FILES src/tools/fs.h DESTINATION include/clippi/tools COMPONENT sdk)
if(WIN32)
  set(linking "Link lib/$<TARGET_FILE_NAME:clippi_core>, psapi.lib and bcrypt.lib.\nUse /MD for Release, RelWithDebInfo and MinSizeRel; /MDd for Debug.")
else()
  set(linking "Link lib/$<TARGET_FILE_NAME:clippi_core> with -pthread -lm.\nCompile C++17 sources with c++, then link objects and the archive with cc.")
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/LINKING-$<CONFIG>.txt" CONTENT
  "Clippi core SDK ($<CONFIG>, ${CMAKE_SYSTEM_NAME})\nUse include/clippi as your include directory and a matching C++17 toolchain.\n${linking}\nNo C++ standard library or networking libraries are required by the SDK.\nAll text ownership follows the arena API.\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/LINKING-$<CONFIG>.txt" DESTINATION . COMPONENT sdk)
