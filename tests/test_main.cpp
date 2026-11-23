#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/arena.h"
#include "base/process_file_lock.h"
#include "base/thread.h"
#include "tests.h"
#include "tools/fs.h"

int g_checks = 0;
int g_failures = 0;
const char* g_test_executable = nullptr;

namespace {

enum SuiteTag { TagDefault, TagNetwork };

struct Suite {
  const char* Name;
  void (*Fn)();
  SuiteTag Tag;
  const char* Desc;
};

const Suite kSuites[] = {
#define CLIPPI_TEST_SUITE(name, fn, tag, description) {name, fn, tag, description},
#include "test_suites.inc"
#undef CLIPPI_TEST_SUITE
};
const u32 kSuiteCount = sizeof(kSuites) / sizeof(kSuites[0]);

bool Contains(const char* haystack, const char* needle) {
  return strstr(haystack, needle) != nullptr;
}

void PrintUsage(const char* exe) {
  printf("usage: %s [options] [suite...]\n\n", exe);
  printf("Runs Clippi's test suites. With no suite arguments, every suite\n");
  printf("except network-tagged ones is run.\n\n");
  printf("options:\n");
  printf("  --list            list available suites and exit\n");
  printf("  --filter <text>   run suites whose name contains <text>\n");
  printf("  --all             include network-tagged suites\n");
  printf("  --benchmark ...   run the agent-loop latency benchmark\n");
  printf("  -h, --help        show this help\n\n");
  printf("examples:\n");
  printf("  %s                       # full offline sweep\n", exe);
  printf("  %s json session          # just those two suites\n", exe);
  printf("  %s --filter line         # linemodel + lineedit\n", exe);
  printf("  %s --all                 # everything, including network\n", exe);
}

void PrintList() {
  printf("%-20s %-8s %s\n", "SUITE", "TAG", "DESCRIPTION");
  for (u32 i = 0; i < kSuiteCount; i++) {
    printf("%-20s %-8s %s\n", kSuites[i].Name,
           kSuites[i].Tag == TagNetwork ? "network" : "default",
           kSuites[i].Desc);
  }
}

// Reserved re-exec subcommand for the cross-process lock test; dispatched before suite selection.
int RunLockProbe(const char* path) {
  Arena arena = ArenaCreate();
  // POSIX F_GETLK ignores self-held locks, so the probe can only be checked cross-process.
  bool held = ProcessFileLock_IsHeld(&arena, SV(path));
  bool survived = Fs_Exists(SV(path));
  ProcessFileLock lock = {};
  ProcessFileLockStatus status =
      ProcessFileLock_TryAcquire(&arena, SV(path), &lock);
  if (status == ProcessFileLockStatus::Acquired)
    ProcessFileLock_Release(&lock, SV(path));
  ArenaRelease(&arena);
  return status == ProcessFileLockStatus::Busy && held && survived ? 0 : 1;
}

bool DrainStdinToFile(const char* path) {
  FILE* out = fopen(path, "ab");
  if (!out) return false;
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) fwrite(buf, 1, n, out);
  fputc('\n', out);
  fclose(out);
  return true;
}

}

int main(int argc, char** argv) {
  if (argc == 3 && strcmp(argv[1], "--echo-arg") == 0) {
    fwrite(argv[2], 1, strlen(argv[2]), stdout);
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--sink-stdin") == 0) {
    return DrainStdinToFile(argv[2]) ? 0 : 1;
  }
  if (argc == 3 && strcmp(argv[1], "--deny-stdin") == 0) {
    if (!DrainStdinToFile(argv[2])) return 1;
    fputs("denied by policy", stdout);
    return 3;
  }
  // Exits 0 while denying, so a passing exit code cannot be what blocks.
  if (argc == 3 && strcmp(argv[1], "--deny-json-stdin") == 0) {
    if (!DrainStdinToFile(argv[2])) return 1;
    fputs("{\"block\":true,\"reason\":\"structured no\",\"terminate\":true}\n", stdout);
    return 0;
  }
  // Exits non-zero while allowing, so the exit code cannot be what allows.
  if (argc == 3 && strcmp(argv[1], "--allow-json-stdin") == 0) {
    if (!DrainStdinToFile(argv[2])) return 1;
    fputs("{\"block\":false}\n", stdout);
    return 7;
  }
  if (argc == 3 && strcmp(argv[1], "--hang") == 0) {
    Sleep_Ms((u32)atoi(argv[2]));
    return 0;
  }
  // Floods past a pipe buffer without reading stdin, deadlocking a parent that drains late.
  if (argc == 3 && strcmp(argv[1], "--flood-stdout") == 0) {
    int lines = atoi(argv[2]);
    for (int i = 0; i < lines; i++)
      fputs("0123456789012345678901234567890123456789012345678901234567890123\n", stdout);
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--expect-lock-busy") == 0) {
    return RunLockProbe(argv[2]);
  }
  if (argc >= 2 && strcmp(argv[1], "--benchmark") == 0)
    return RunAgentBenchmark(argc - 1, argv + 1);

  g_test_executable = argv[0];

  bool selected[kSuiteCount] = {};
  bool anySelected = false;
  bool includeNetwork = false;
  bool unknownArg = false;

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      PrintUsage(argv[0]);
      return 0;
    }
    if (strcmp(arg, "--list") == 0) {
      PrintList();
      return 0;
    }
    if (strcmp(arg, "--all") == 0) {
      includeNetwork = true;
      continue;
    }
    if (strcmp(arg, "--filter") == 0) {
      if (i + 1 >= argc) {
        printf("error: --filter requires an argument\n");
        return 2;
      }
      const char* needle = argv[++i];
      bool matched = false;
      for (u32 s = 0; s < kSuiteCount; s++) {
        if (Contains(kSuites[s].Name, needle)) {
          selected[s] = true;
          anySelected = true;
          matched = true;
        }
      }
      if (!matched) {
        printf("error: no suite matches filter '%s'\n", needle);
        unknownArg = true;
      }
      continue;
    }
    if (arg[0] == '-') {
      printf("error: unknown option '%s'\n", arg);
      unknownArg = true;
      continue;
    }
    bool matched = false;
    for (u32 s = 0; s < kSuiteCount; s++) {
      if (strcmp(kSuites[s].Name, arg) == 0) {
        selected[s] = true;
        anySelected = true;
        matched = true;
        break;
      }
    }
    if (!matched) {
      printf("error: unknown suite '%s'\n", arg);
      unknownArg = true;
    }
  }

  if (unknownArg) {
    printf("run '%s --list' to see available suites\n", argv[0]);
    return 2;
  }

  if (!anySelected) {
    for (u32 s = 0; s < kSuiteCount; s++) {
      selected[s] = includeNetwork || kSuites[s].Tag == TagDefault;
    }
  }

  u32 ran = 0;
  for (u32 s = 0; s < kSuiteCount; s++) {
    if (!selected[s]) continue;
    int before = g_failures;
    printf("[ run  ] %s\n", kSuites[s].Name);
    kSuites[s].Fn();
    ran++;
    printf("[ %s ] %s\n", g_failures == before ? " ok  " : "FAIL!",
           kSuites[s].Name);
  }

  printf("tests: %u suites, %d checks, %d failures\n", ran, g_checks,
         g_failures);
  return g_failures ? 1 : 0;
}
