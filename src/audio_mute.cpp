// Mute this process's audio session (WASAPI). Runtime-only: never touches the game's saved volume settings.
#include "audio_mute.h"
#include "log.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

namespace gd::audio {
bool mute_process(bool mute) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool uninit = SUCCEEDED(hr);
  IMMDeviceEnumerator* en = nullptr;
  IMMDevice* dev = nullptr;
  IAudioSessionManager* mgr = nullptr;
  ISimpleAudioVolume* vol = nullptr;
  bool ok = false;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) &&
      SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
      SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr, (void**)&mgr)) &&
      SUCCEEDED(mgr->GetSimpleAudioVolume(nullptr, FALSE, &vol))) {
    ok = SUCCEEDED(vol->SetMute(mute ? TRUE : FALSE, nullptr));
  }
  if (vol) vol->Release();
  if (mgr) mgr->Release();
  if (dev) dev->Release();
  if (en) en->Release();
  if (uninit) CoUninitialize();
  log::writef("audio: process session mute={} -> {}", mute, ok ? "ok" : "failed");
  return ok;
}
}  // namespace gd::audio
