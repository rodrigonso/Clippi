#pragma once
#include "base/str.h"
#include <stdio.h>

enum class FileIoResult : u8 {
  Success,
  Missing,
  OpenFailed,
  IoError,
  TooLarge,
};

constexpr u64 FILE_READ_MAX_BYTES = 0xFFFFFFFEull;

FileIoResult File_Open(Arena* arena, StrView path, const char* mode, FILE** out);

struct FileReader {
  void* Handle;
};

// A successful zero-byte read means EOF; close clears the handle even on error.
FileIoResult File_OpenRead(Arena* scratch, StrView path, FileReader* out);
FileIoResult File_ReadChunk(FileReader* file, void* buffer, u32 capacity, u32* count);
FileIoResult File_Close(FileReader* file);

// Successful reads are null-terminated at [Length]; failed reads give back any arena space they used.
FileIoResult File_ReadAll(Arena* arena, StrView path, StrView* out,
                          u64 maxBytes = FILE_READ_MAX_BYTES);

// Truncates in place. Configuration files must use ConfigFile_WriteAtomic.
FileIoResult File_WriteAll(Arena* arena, StrView path, StrView content);

// Narrow CRT calls take the ANSI code page on Windows, which cannot name every path.
bool File_Remove(Arena* arena, StrView path);
bool File_Replace(Arena* arena, StrView from, StrView to);
// Unlike File_Replace this works for directories on Windows; `to` must not exist.
bool File_Rename(Arena* arena, StrView from, StrView to);
bool File_MakeDir(Arena* arena, StrView path);
// Records from concurrent appenders, including other processes, never interleave.
FileIoResult File_Append(Arena* arena, StrView path, StrView content);
