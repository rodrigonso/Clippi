# Embeds prompts/*.md into `target` as StrView constants named PROMPT_<STEM>.
# Line endings become LF and one trailing newline is dropped, so a CRLF
# checkout produces the same bytes; prompt parity tests depend on that.
function(clippi_embed_prompts target)
  file(GLOB prompt_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/prompts/*.md")
  list(SORT prompt_files)
  set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/prompts")
  set(banner "// Generated from prompts/*.md by cmake/prompts.cmake; edit those files.\n")
  set(header "${banner}#pragma once\n#include \"base/str.h\"\n\n")
  set(source "${banner}#include \"prompts/prompt_text.h\"\n\n")
  set(table "")
  foreach(path IN LISTS prompt_files)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    get_filename_component(name "${path}" NAME)
    get_filename_component(stem "${path}" NAME_WE)
    string(TOUPPER "PROMPT_${stem}" symbol)
    string(REGEX REPLACE "[^A-Z0-9_]" "_" symbol "${symbol}")
    file(READ "${path}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(REGEX REPLACE "\n$" "" text "${text}")
    string(FIND "${text}" ")CLIPPI_PROMPT\"" clash)
    if(NOT clash EQUAL -1)
      message(FATAL_ERROR "${name} contains the embedding delimiter )CLIPPI_PROMPT\"")
    endif()
    string(LENGTH "${text}" size)
    # MSVC rejects a single string literal past 16 KB.
    if(size GREATER 16000)
      message(FATAL_ERROR "${name} is ${size} bytes; split it below 16000")
    endif()
    string(APPEND header "extern const StrView ${symbol};\n")
    string(APPEND source
      "extern constexpr StrView ${symbol} =\n    SVL(R\"CLIPPI_PROMPT(${text})CLIPPI_PROMPT\");\n\n")
    string(APPEND table "    {SVL(\"${name}\"), ${symbol}},\n")
  endforeach()
  string(APPEND header
    "\nstruct PromptText {\n  StrView Name;\n  StrView Text;\n};\n\n"
    "extern const PromptText PROMPT_TEXTS[];\nextern const u32 PROMPT_TEXT_COUNT;\n")
  string(APPEND source
    "extern constexpr PromptText PROMPT_TEXTS[] = {\n${table}};\n"
    "extern constexpr u32 PROMPT_TEXT_COUNT = sizeof(PROMPT_TEXTS) / sizeof(PROMPT_TEXTS[0]);\n")
  # configure_file only rewrites on change, so an unrelated reconfigure rebuilds nothing.
  file(WRITE "${out_dir}/prompt_text.h.tmp" "${header}")
  file(WRITE "${out_dir}/prompt_text.cpp.tmp" "${source}")
  configure_file("${out_dir}/prompt_text.h.tmp" "${out_dir}/prompt_text.h" COPYONLY)
  configure_file("${out_dir}/prompt_text.cpp.tmp" "${out_dir}/prompt_text.cpp" COPYONLY)
  target_sources(${target} PRIVATE "${out_dir}/prompt_text.cpp")
  target_include_directories(${target} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>")
endfunction()
