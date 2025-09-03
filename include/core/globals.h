#ifndef GLOBALS_H
#define GLOBALS_H

#include <atomic>

// Global shutdown flag - declare once, define in main.cpp
extern std::atomic<bool> g_shutdown_requested;

#endif // GLOBALS_H