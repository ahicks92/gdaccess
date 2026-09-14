#include "crash.h"
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "log.h"

// Why a vectored handler and not SetUnhandledExceptionFilter: the game installs its own reporter, and the fault
// that motivated this (VM report 2026-09-14: the cast tracker called through a freed object's vtable, the garbage
// corrupted the heap, ntdll fast-failed the process) never reached any unhandled filter. A first-chance VEH sees
// every exception on its own thread before any frame handler. It also sees the faults our __try probes and
// IsBadReadPtr take on purpose, so ordinary codes are deduplicated per (code, address) and capped; the codes that
// mean the process is about to die are always written.
//
// Rules inside the handler: no heap (the heap may be what broke), no loader lock (no GetModuleHandleEx /
// GetModuleFileName), no C++ objects; stack buffers + a dedicated file handle only.
namespace gd::crash {
namespace {
struct Module { uintptr_t lo, hi; char name[32]; };
Module g_mods[64]; int g_nmods = 0;
HANDLE g_file = INVALID_HANDLE_VALUE;
void* g_handler = nullptr;
thread_local bool t_inside = false;
struct Seen { DWORD code; uintptr_t addr; unsigned n; };
Seen g_seen[64]; int g_nseen = 0;
long g_lines = 0;
constexpr long kMaxLines = 200;

void snapshot_modules() {
  HMODULE mods[256]; DWORD need = 0;
  if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return;
  int n = (int)(need / sizeof(HMODULE)); if (n > 256) n = 256;
  g_nmods = 0;
  for (int i = 0; i < n && g_nmods < 64; ++i) {
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof mi)) continue;
    Module& m = g_mods[g_nmods];
    m.lo = (uintptr_t)mi.lpBaseOfDll; m.hi = m.lo + mi.SizeOfImage;
    if (!GetModuleBaseNameA(GetCurrentProcess(), mods[i], m.name, sizeof m.name)) strcpy_s(m.name, "?");
    ++g_nmods;
  }
}
const Module* module_of(uintptr_t a) {
  for (int i = 0; i < g_nmods; ++i) if (a >= g_mods[i].lo && a < g_mods[i].hi) return &g_mods[i];
  return nullptr;
}
// "Game.dll+0x1234" for a known module, "image@<base>+0x.." for one loaded after the snapshot, false otherwise.
bool describe(uintptr_t a, char* out, size_t n) {
  if (const Module* m = module_of(a)) { snprintf(out, n, "%s+0x%llx", m->name, (unsigned long long)(a - m->lo)); return true; }
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery((const void*)a, &mbi, sizeof mbi) && mbi.Type == MEM_IMAGE && mbi.AllocationBase) {
    snprintf(out, n, "image@%p+0x%llx", mbi.AllocationBase, (unsigned long long)(a - (uintptr_t)mbi.AllocationBase));
    return true;
  }
  return false;
}
bool fatal_code(const EXCEPTION_RECORD* er) {
  switch (er->ExceptionCode) {
    case 0xC0000374:   // STATUS_HEAP_CORRUPTION
    case 0xC0000409:   // STATUS_STACK_BUFFER_OVERRUN / fast fail
    case 0xC000001D:   // illegal instruction
    case 0xC0000096:   // privileged instruction
    case 0xC00000FD:   // stack overflow
      return true;
    case 0xC0000005:   // an execute fault (jumped into non-code) is fatal in practice; a read/write fault may be a probe
      return er->NumberParameters >= 2 && er->ExceptionInformation[0] == 8;
  }
  return false;
}
// Ordinary codes: log the first 3 of each (code, address), then every 100th; at most kMaxLines per load.
bool should_log(const EXCEPTION_RECORD* er) {
  if (fatal_code(er)) return true;
  if (g_lines >= kMaxLines) return false;
  uintptr_t a = (uintptr_t)er->ExceptionAddress;
  for (int i = 0; i < g_nseen; ++i)
    if (g_seen[i].code == er->ExceptionCode && g_seen[i].addr == a) { unsigned n = ++g_seen[i].n; return n <= 3 || n % 100 == 0; }
  if (g_nseen < 64) g_seen[g_nseen++] = Seen{er->ExceptionCode, a, 1};
  return true;
}
void raw_write(const char* s) {
  if (g_file == INVALID_HANDLE_VALUE) return;
  DWORD w = 0; WriteFile(g_file, s, (DWORD)strlen(s), &w, nullptr);
  FlushFileBuffers(g_file);
}

LONG CALLBACK handler(EXCEPTION_POINTERS* ep) {
  if (t_inside || !ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  DWORD code = er->ExceptionCode;
  if ((code >> 30) != 3) return EXCEPTION_CONTINUE_SEARCH;            // errors only (no debug notices, no thread-name 0x406D1388)
  if (code == 0xE06D7363 || code == 0xE0434352) return EXCEPTION_CONTINUE_SEARCH;   // C++ / CLR throws are not faults
  if (!should_log(er)) return EXCEPTION_CONTINUE_SEARCH;
  t_inside = true;
  __try {
    ++g_lines;
    SYSTEMTIME t; GetLocalTime(&t);
    char where[96]; if (!describe((uintptr_t)er->ExceptionAddress, where, sizeof where)) snprintf(where, sizeof where, "%p", er->ExceptionAddress);
    char line[1024]; int len = 0;
    len += snprintf(line + len, sizeof line - len, "%02u:%02u:%02u.%03u crash: exception 0x%08lx at %s thread %lu%s", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, code, where, GetCurrentThreadId(), fatal_code(er) ? " FATAL" : "");
    if (code == 0xC0000005 && er->NumberParameters >= 2)
      len += snprintf(line + len, sizeof line - len, " (%s %p)", er->ExceptionInformation[0] == 8 ? "execute" : er->ExceptionInformation[0] == 1 ? "write" : "read", (void*)er->ExceptionInformation[1]);
    if (ep->ContextRecord) {
      const CONTEXT* c = ep->ContextRecord;
      len += snprintf(line + len, sizeof line - len, " rip=%p rsp=%p rcx=%p rdx=%p rax=%p", (void*)c->Rip, (void*)c->Rsp, (void*)c->Rcx, (void*)c->Rdx, (void*)c->Rax);
    }
    len += snprintf(line + len, sizeof line - len, "\n");
    raw_write(line);
    // Return-address scan: every qword on the faulting thread's stack (from rsp up, within the TEB's limits) that
    // points into a loaded image. Coarser than an unwind but it survives a garbage frame, which an unwind does not.
    if (ep->ContextRecord && code != 0xC00000FD) {
      const NT_TIB* tib = (const NT_TIB*)NtCurrentTeb();
      uintptr_t sp = (uintptr_t)ep->ContextRecord->Rsp & ~(uintptr_t)7, base = (uintptr_t)tib->StackBase, limit = (uintptr_t)tib->StackLimit;
      if (sp >= limit && sp < base) {
        len = snprintf(line, sizeof line, "  callers:");
        int hits = 0;
        for (uintptr_t p = sp; p + 8 <= base && p < sp + 4096 && hits < 16; p += 8) {
          uintptr_t v = *(const uintptr_t*)p;
          if (!module_of(v)) continue;          // only known images: VirtualQuery per slot is too slow for 512 slots
          char d[96]; describe(v, d, sizeof d);
          len += snprintf(line + len, sizeof line - len, " %s", d);
          ++hits;
        }
        len += snprintf(line + len, sizeof line - len, "\n");
        raw_write(line);
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  t_inside = false;
  return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void install() {
  snapshot_modules();
  std::wstring p = log::path();
  if (!p.empty())
    g_file = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  g_handler = AddVectoredExceptionHandler(1, handler);
  log::writef("crash: logger installed ({} modules, handler {})", g_nmods, g_handler ? "ok" : "FAILED");
}
void remove() {
  if (g_handler) { RemoveVectoredExceptionHandler(g_handler); g_handler = nullptr; }
  if (g_file != INVALID_HANDLE_VALUE) { CloseHandle(g_file); g_file = INVALID_HANDLE_VALUE; }
}
}  // namespace gd::crash
