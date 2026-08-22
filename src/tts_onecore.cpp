#include "tts_onecore.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include "log.h"

#pragma warning(push)
#pragma warning(disable : 4265 4471 4996 5204 5205 5246)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.SpeechSynthesis.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma warning(pop)

namespace gd::tts {
namespace {
using namespace winrt::Windows::Media::SpeechSynthesis;
using namespace winrt::Windows::Storage::Streams;

bool g_ready = false;
std::string g_error;
double g_last_ms = 0;
// Two synthesizers so each slot's voice is set once and an utterance never races a voice change.
SpeechSynthesizer g_synth[2] = {nullptr, nullptr};  // projected types: null until init
std::string g_selected[2];

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string err_text(const winrt::hresult_error& e) { return std::format("{:#x} {}", (uint32_t)e.code(), winrt::to_string(e.message())); }

// The first WinRT touch, with no C++ objects that need unwinding: a delay-load failure of an api-ms-win-*
// set arrives as a structured exception, which a C++ catch would not see.
bool probe_body() { winrt::hstring h = winrt::to_hstring("probe"); return h.size() == 5; }
bool probe_seh() {
  __try { return probe_body(); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}  // namespace

void enter_apartment() { try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (const winrt::hresult_error& e) { g_error = err_text(e); } }
void leave_apartment() { try { winrt::uninit_apartment(); } catch (...) {} }

bool init() {
  if (g_ready) return true;
  g_error.clear();
  if (!probe_seh()) { g_error = "WinRT runtime not available in this process (delay-load failed)"; log::writef("tts: {}", g_error); return false; }
  try {
    for (int i = 0; i < 2; ++i) { g_synth[i] = SpeechSynthesizer(); g_selected[i] = winrt::to_string(g_synth[i].Voice().DisplayName()); }
    g_ready = true;
    log::writef("tts: OneCore ready, {} voices installed, default '{}'", SpeechSynthesizer::AllVoices().Size(), g_selected[0]);
    return true;
  } catch (const winrt::hresult_error& e) {
    g_error = err_text(e);
    log::writef("tts: init failed: {}", g_error);
    shutdown();
    return false;
  }
}

void shutdown() {
  g_ready = false;
  for (int i = 0; i < 2; ++i) g_synth[i] = nullptr;
}
bool ready() { return g_ready; }

std::vector<VoiceInfo> voices() {
  std::vector<VoiceInfo> out;
  try {
    for (const VoiceInformation& v : SpeechSynthesizer::AllVoices())
      out.push_back({winrt::to_string(v.Id()), winrt::to_string(v.DisplayName()), winrt::to_string(v.Language())});
  } catch (const winrt::hresult_error& e) { g_error = err_text(e); }
  return out;
}

bool select(int slot, std::string_view name_contains) {
  if (slot < 0 || slot > 1 || !g_ready || !g_synth[slot]) return false;
  std::string want = lower(std::string(name_contains));
  try {
    for (const VoiceInformation& v : SpeechSynthesizer::AllVoices()) {
      std::string name = winrt::to_string(v.DisplayName());
      if (lower(name).find(want) != std::string::npos) {
        g_synth[slot].Voice(v);
        g_selected[slot] = name;
        log::writef("tts: slot {} = '{}' ({})", slot, name, winrt::to_string(v.Language()));
        return true;
      }
    }
    VoiceInformation d = SpeechSynthesizer::DefaultVoice();
    g_synth[slot].Voice(d);
    g_selected[slot] = winrt::to_string(d.DisplayName());
    log::writef("tts: no installed voice matches '{}'; slot {} uses the default '{}'", name_contains, slot, g_selected[slot]);
  } catch (const winrt::hresult_error& e) { g_error = err_text(e); log::writef("tts: select failed: {}", g_error); }
  return false;
}
std::string selected(int slot) { return slot >= 0 && slot < 2 ? g_selected[slot] : std::string(); }

bool synthesize(int slot, std::string_view utf8, std::vector<uint8_t>& wav_out) {
  if (slot < 0 || slot > 1 || !g_ready || !g_synth[slot]) return false;
  auto t0 = std::chrono::steady_clock::now();
  try {
    SpeechSynthesisStream stream = g_synth[slot].SynthesizeTextToStreamAsync(winrt::to_hstring(std::string(utf8))).get();
    uint32_t size = (uint32_t)stream.Size();
    DataReader reader(stream.GetInputStreamAt(0));
    reader.LoadAsync(size).get();
    wav_out.resize(size);
    reader.ReadBytes(winrt::array_view<uint8_t>(wav_out.data(), wav_out.data() + size));
    reader.Close();
    stream.Close();
  } catch (const winrt::hresult_error& e) {
    g_error = err_text(e);
    log::writef("tts: synthesize failed: {}", g_error);
    return false;
  }
  g_last_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return !wav_out.empty();
}
std::string last_error() { return g_error; }
double last_ms() { return g_last_ms; }
}  // namespace gd::tts
