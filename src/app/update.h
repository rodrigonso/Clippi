#pragma once
#include "base/arena.h"
#include "base/str.h"
#include "base/thread.h"

#define CLIPPI_REPOSITORY "rodrigonso/Clippi"
#define CLIPPI_RELEASES_API_URL \
  "https://api.github.com/repos/" CLIPPI_REPOSITORY "/releases?per_page=1"
#define CLIPPI_INSTALL_SCRIPT_BASE_URL \
  "https://raw.githubusercontent.com/" CLIPPI_REPOSITORY "/main/tools/install/"

bool Version_Parse(StrView text, u32 out[3]);
// Unparsable versions compare equal, so garbage never triggers a notice.
i32 Version_Compare(StrView a, StrView b);

// Prereleases count, because every public-alpha release is one.
StrView Releases_ParseLatest(Arena* arena, StrView json);

StrView Install_PrefixFromExe(Arena* arena, StrView exePath);
StrView Install_Prefix(Arena* arena);

int RunUpgrade(Arena* arena, const char* version);

// The notice comes from a recorded check, so startup never waits on the
// network.

struct UpdateCache {
  u64 CheckedAtMs;
  StrView Latest;
};

bool UpdateCache_Parse(Arena* arena, StrView json, UpdateCache* out);
StrView UpdateCache_Format(Arena* arena, const UpdateCache* cache);
bool UpdateCache_IsStale(const UpdateCache* cache, u64 nowMs);

StrView UpdateNotice(Arena* arena, const UpdateCache* cache, StrView current);

struct UpdateCheck {
  Thread Worker;
  bool Running;
  volatile i32 Abort;
  StrView CachePath;  // owned by the caller's arena
  UpdateCache Cached; // as read at startup; owned by the caller's arena
  StrView Notice;
};

// Requires Http_GlobalInit.
void UpdateCheck_Start(UpdateCheck* check, Arena* arena);
// Must run before Http_GlobalShutdown.
void UpdateCheck_Stop(UpdateCheck* check);
