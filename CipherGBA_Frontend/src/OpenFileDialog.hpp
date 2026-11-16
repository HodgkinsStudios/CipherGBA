#pragma once
#include <string>
// Returns an absolute UTF-8 path or empty string when cancelled/unavailable.
// Invoked synchronously from the SDL2 main event thread.
namespace ciphergba_host {
std::string openFileDialog(bool bios);
}
