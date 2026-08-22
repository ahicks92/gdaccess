#pragma once
// The OneCore text-to-speech backend (WinRT Windows.Media.SpeechSynthesis), rendering text to WAV bytes. The
// only translation unit that includes C++/WinRT; this header leaks no WinRT type. Every function here is
// WORKER-THREAD ONLY (src/voice.cpp's thread, an MTA apartment): the blocking .get() on the synthesis
// operation throws on an STA, and the game's own threads are never touched.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace gd::tts {
struct VoiceInfo { std::string id, display_name, language; };
void enter_apartment();              // winrt::init_apartment(multi_threaded) for the calling (worker) thread
void leave_apartment();
bool init();                         // creates the synthesizers; false = OneCore is not usable here (see last_error)
void shutdown();                     // releases every WinRT object (call on the worker before it exits)
bool ready();
std::vector<VoiceInfo> voices();     // SpeechSynthesizer::AllVoices()
// Bind a slot (0 or 1) to the first installed voice whose display name contains name_contains (case-
// insensitive); falls back to the default voice with a loud log line. Returns true on an exact find.
bool select(int slot, std::string_view name_contains);
std::string selected(int slot);      // the display name actually bound
// Render utf8 text with the slot's voice into WAV bytes (16-bit PCM, the voice's native rate). Blocking.
bool synthesize(int slot, std::string_view utf8, std::vector<uint8_t>& wav_out);
std::string last_error();
double last_ms();                    // wall time of the last synthesis
}  // namespace gd::tts
