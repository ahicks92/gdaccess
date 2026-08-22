#pragma once
namespace gd::dev {
// Loopback HTTP server for driving and observing the mod while the game runs (the "dev loop").
// Routes (all GET unless noted; responses are plain text):
//   /health                     liveness: pid, frame, backend, mute states
//   /speech?since=N             spoken lines since cursor N ("cursor: M" first, then "i: text")
//   /log?since=N                mod log lines since cursor N
//   /text                       the last frame's captured on-screen text: x y xalign yalign rgba variant \t text
//   /say?text=...               speak (through the mod's speech path)
//   /mute?on=1|0                mute/unmute mod speech (history still records)
//   /gamekeys?on=1|0            1 = the game sees no physical keys
//   /key?code=0x1e&ch=a&shift=&ctrl=&alt=   synthetic key press+release; or ?name=enter|escape|up|...
//   /keys?text=abc              type characters
//   /cursor?x=&y= | ?clear=1    override the game's cursor position
//   /buttons                    which Button values the game polls via IsButtonDown
void start(int port);
void stop();
}  // namespace gd::dev
