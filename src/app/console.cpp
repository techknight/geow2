#include "app/console.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#endif

namespace geow2 {

void attach_parent_console() {
#ifdef _WIN32
  auto missing = [](DWORD which) {
    const HANDLE h = GetStdHandle(which);
    return !h || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN;
  };
  const bool no_out = missing(STD_OUTPUT_HANDLE), no_err = missing(STD_ERROR_HANDLE);
  if ((no_out || no_err) && AttachConsole(ATTACH_PARENT_PROCESS)) {
    if (no_out) std::freopen("CONOUT$", "w", stdout);
    if (no_err) std::freopen("CONOUT$", "w", stderr);
  }
#endif
}

}  // namespace geow2
