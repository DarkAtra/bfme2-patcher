#pragma once

#include <cstdint>

bool applyRenderThread();

// the thread that runs the game (the one that loads the dll), 0 until applyRenderThread ran
uint32_t renderThreadGameThread();
