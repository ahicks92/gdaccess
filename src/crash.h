#pragma once
// Crash logger: a vectored exception handler that writes every serious exception's code, address and the return
// addresses found on the faulting thread's stack (module+rva) to the mod log, with no heap use, so a fault that
// kills the process before the game's crash reporter runs (heap corruption fast-fails) still leaves its location.
namespace gd::crash {
void install();   // right after log::init
void remove();
}  // namespace gd::crash
