#pragma once

// Test hooks of the null backend (gb_null.cpp): the calls it receives, one line each, for tests/gfx_d3d9emu.

#include <string>
#include <vector>

namespace gb::null
{
// While recording, every call is appended to the log; resources are named by creation order (B1, T2, ...).
void SetRecording(bool enabled);
std::vector<std::string> TakeLog();
} // namespace gb::null
