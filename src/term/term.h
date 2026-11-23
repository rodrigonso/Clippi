#pragma once
#include "base/basics.h"
#include "base/str.h"

struct Term {
  bool Active;
#if defined(_WIN32)
  void* HOut;
  void* HIn;
  u32 SavedOutMode;
  u32 SavedInMode;
  u32 SavedCP;
#else
  int InFd;
  int OutFd;
  int WakeReadFd;
  int WakeWriteFd;
  // Opaque `struct termios` (72 bytes on 64-bit macOS) with headroom, so term.h need not include <termios.h>.
  alignas(8) unsigned char SavedTermios[128];
#endif
};

bool Term_IsInteractive();

bool Term_SystemAccent(u8* r, u8* g, u8* b);

// Returns false (and does nothing) if not interactive. Restoration is also armed via atexit.
bool Term_Enter(Term* t);

// Idempotent.
void Term_Exit(Term* t);

void Term_Size(const Term* t, u16* cols, u16* rows);

void Term_Write(const char* data, u32 len);
void Term_WriteStr(const char* s);

const char* Term_SetupSequence();
const char* Term_TeardownSequence();

// Call on the render/output thread so the sequence stays ordered with screen output.
void Term_AppendOSC52(StrBuf* out, StrView data);

u32 Term_ReadInput(const Term* t, char* buf, u32 cap);

bool Term_WaitInput(const Term* t, u32 timeoutMs);

void Term_WakeInput(const Term* t);

void Term_TrackForRestore(Term* t);
