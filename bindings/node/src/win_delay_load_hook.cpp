// Windows Node-API addons import Node symbols from node.exe. Electron exports
// the same symbols from its renamed host executable, so the import must be
// delayed and redirected to the current process image.

#if defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <delayimp.h>
#include <string.h>

namespace {

FARPROC WINAPI redirect_node_host(unsigned int notification,
                                  DelayLoadInfo* information) {
  if (notification != dliNotePreLoadLibrary || information == nullptr ||
      information->szDll == nullptr ||
      _stricmp(information->szDll, "node.exe") != 0) {
    return nullptr;
  }

  // Preserve compatibility with Node builds that expose symbols from
  // libnode.dll; otherwise Node and Electron both resolve through their
  // current executable image.
  HMODULE host = GetModuleHandleW(L"libnode.dll");
  if (host == nullptr) host = GetModuleHandleW(nullptr);
  return reinterpret_cast<FARPROC>(host);
}

}  // namespace

decltype(__pfnDliNotifyHook2) __pfnDliNotifyHook2 = redirect_node_host;

#endif
