#pragma once
#include <string_view>
#include "ring.h"
namespace gd::speech {
bool init();            // loads prism, picks the best available backend
void shutdown();
void speak(std::string_view utf8, bool interrupt = true);
void stop();
const char* backend_name();
void set_muted(bool m);  // muted: nothing reaches the backend, but history still records
bool muted();
LineRing& history();     // every line ever passed to speak(), for the dev server
}  // namespace gd::speech
