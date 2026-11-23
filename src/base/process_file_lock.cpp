#include "base/process_file_lock.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>

#include "base/win32_utf8.h"

static_assert(sizeof(HANDLE) <= sizeof(((ProcessFileLock*)nullptr)->Storage),
              "ProcessFileLock storage too small");

ProcessFileLockStatus ProcessFileLock_TryAcquire(Arena* scratch, StrView path,
                                                 ProcessFileLock* out) {
  if (!scratch || !out || path.Length == 0) return ProcessFileLockStatus::IoError;
  *out = ProcessFileLock{};
  wchar_t native[4096];
  if (!Win32_Path(path, native, 4096)) return ProcessFileLockStatus::IoError;
  // DELETE_ON_CLOSE ties the sentinel to the handle, so the kernel removes it even if this process is killed.
  HANDLE handle = CreateFileW(native,
                              GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    // A holder's pending delete blocks reopening; that means held, not an I/O failure.
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
        error == ERROR_ACCESS_DENIED || error == ERROR_DELETE_PENDING)
      return ProcessFileLockStatus::Busy;
    return ProcessFileLockStatus::IoError;
  }
  memcpy(out->Storage, &handle, sizeof(handle));
  out->Held = true;
  return ProcessFileLockStatus::Acquired;
}

void ProcessFileLock_Release(ProcessFileLock* lock, StrView path) {
  (void)path;  // The open handle carries the deletion; see TryAcquire.
  if (!lock || !lock->Held) return;
  HANDLE handle = nullptr;
  memcpy(&handle, lock->Storage, sizeof(handle));
  CloseHandle(handle);
  *lock = ProcessFileLock{};
}

bool ProcessFileLock_IsHeld(Arena* scratch, StrView path) {
  if (!scratch || path.Length == 0) return false;
  wchar_t native[4096];
  if (!Win32_Path(path, native, 4096)) return false;
  // GENERIC_READ is required: an access-free open bypasses the sharing check and reports a held lock as free.
  HANDLE handle =
      CreateFileW(native, GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
           error == ERROR_ACCESS_DENIED || error == ERROR_DELETE_PENDING;
  }
  CloseHandle(handle);
  return false;
}

#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static_assert(sizeof(int) <= sizeof(((ProcessFileLock*)nullptr)->Storage),
              "ProcessFileLock storage too small");

namespace {

bool NativePath(StrView path, char* buffer, u32 capacity) {
  if (path.Length == 0 || path.Length + 1 > capacity) return false;
  memcpy(buffer, path.Data, path.Length);
  buffer[path.Length] = '\0';
  return true;
}

}

ProcessFileLockStatus ProcessFileLock_TryAcquire(Arena* scratch, StrView path,
                                                 ProcessFileLock* out) {
  if (!scratch || !out || path.Length == 0) return ProcessFileLockStatus::IoError;
  *out = ProcessFileLock{};
  const char* target = StrView_DupCStr(scratch, path);

  // open() can race an unlinking holder and land on a dead inode, letting two processes hold
  // the lock; the inode is rechecked after locking and a losing attempt is retried.
  for (u32 attempt = 0; attempt < 8; attempt++) {
    int fd = open(target, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return ProcessFileLockStatus::IoError;

    struct flock claim = {};
    claim.l_type = F_WRLCK;
    claim.l_whence = SEEK_SET;
    claim.l_start = 0;
    claim.l_len = 0;
    if (fcntl(fd, F_SETLK, &claim) != 0) {
      int error = errno;
      close(fd);
      if (error == EACCES || error == EAGAIN) return ProcessFileLockStatus::Busy;
      return ProcessFileLockStatus::IoError;
    }

    struct stat locked = {};
    struct stat published = {};
    if (fstat(fd, &locked) != 0) {
      close(fd);
      return ProcessFileLockStatus::IoError;
    }
    if (stat(target, &published) == 0) {
      if (locked.st_dev == published.st_dev && locked.st_ino == published.st_ino) {
        memcpy(out->Storage, &fd, sizeof(fd));
        out->Held = true;
        return ProcessFileLockStatus::Acquired;
      }
    } else if (errno != ENOENT) {
      close(fd);
      return ProcessFileLockStatus::IoError;
    }
    close(fd);
  }
  // Persistently losing the race means another process is cycling the lock.
  return ProcessFileLockStatus::Busy;
}

void ProcessFileLock_Release(ProcessFileLock* lock, StrView path) {
  if (!lock || !lock->Held) return;
  int fd = -1;
  memcpy(&fd, lock->Storage, sizeof(fd));
  // Unlink before dropping the lock, so a racing acquirer fails F_SETLK or sees the inode change.
  char native[4096];
  if (NativePath(path, native, (u32)sizeof(native))) unlink(native);
  close(fd);
  *lock = ProcessFileLock{};
}

bool ProcessFileLock_IsHeld(Arena* scratch, StrView path) {
  (void)scratch;
  char native[4096];
  if (!NativePath(path, native, (u32)sizeof(native))) return false;
  // No O_CREAT: probing must never author a sentinel.
  int fd = open(native, O_RDONLY);
  if (fd < 0) return false;
  struct flock probe = {};
  probe.l_type = F_WRLCK;
  probe.l_whence = SEEK_SET;
  probe.l_start = 0;
  probe.l_len = 0;
  bool held = fcntl(fd, F_GETLK, &probe) == 0 && probe.l_type != F_UNLCK;
  close(fd);
  return held;
}
#endif
