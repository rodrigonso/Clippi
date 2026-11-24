include(FetchContent)
set(CLIPPI_PCRE2_ARCHIVE "" CACHE FILEPATH "Offline copy of the pinned PCRE2 release")
set(pcre2_version 10.48)
set(pcre2_hash ebcc25aadf2a51fa1fefa9b8bc9e7a79b3dae86870a0f1152a22e42befd46888)

clippi_pinned_url(pcre2_url PCRE2
  "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${pcre2_version}/pcre2-${pcre2_version}.tar.gz"
  "${pcre2_hash}" "${CLIPPI_PCRE2_ARCHIVE}")

block(SCOPE_FOR VARIABLES PROPAGATE pcre2_SOURCE_DIR)
  foreach(option
      BUILD_SHARED_LIBS PCRE2_BUILD_PCRE2_16 PCRE2_BUILD_PCRE2_32
      PCRE2_BUILD_PCRE2GREP PCRE2_BUILD_TESTS PCRE2_SUPPORT_JIT
      PCRE2_SUPPORT_JIT_SEALLOC PCRE2GREP_SUPPORT_JIT PCRE2GREP_SUPPORT_CALLOUT
      PCRE2GREP_SUPPORT_CALLOUT_FORK PCRE2_SUPPORT_LIBBZ2 PCRE2_SUPPORT_LIBZ
      PCRE2_SUPPORT_LIBEDIT PCRE2_SUPPORT_LIBREADLINE PCRE2_SUPPORT_VALGRIND
      PCRE2_REBUILD_CHARTABLES PCRE2_EBCDIC PCRE2_EBCDIC_NL25
      PCRE2_EBCDIC_IGNORING_COMPILER PCRE2_SHOW_REPORT INSTALL_MSVC_PDB)
    set(${option} OFF CACHE BOOL "" FORCE)
  endforeach()
  foreach(option BUILD_STATIC_LIBS PCRE2_BUILD_PCRE2_8 PCRE2_SUPPORT_UNICODE PCRE2_NEVER_BACKSLASH_C)
    set(${option} ON CACHE BOOL "" FORCE)
  endforeach()
  set(PCRE2_NEWLINE LF CACHE STRING "" FORCE)
  set(PCRE2_LINK_SIZE 2 CACHE STRING "" FORCE)

  # Upstream probes these optional tool dependencies even when its tools are off.
  foreach(package BZip2 ZLIB Readline Editline)
    set(CMAKE_DISABLE_FIND_PACKAGE_${package} TRUE)
  endforeach()
  set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
  set(FETCHCONTENT_SOURCE_DIR_PCRE2 "")
  # EXCLUDE_FROM_ALL also keeps PCRE2's own install rules out of our packages.
  FetchContent_Declare(pcre2
    URL "${pcre2_url}" URL_HASH "SHA256=${pcre2_hash}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(pcre2)
endblock()

set(CLIPPI_PCRE2_TARGET pcre2-8-static)
