// MemoryScanner.h - helper for finding where the local player's position lives (reverse
// engineering aid used by `mcradar --scan x y z`). Not used by the radar loop itself.
#pragma once
#include "Platform.h"
#include <vector>

// Walks the target's committed, readable, private memory (the Java heap lives there) with
// VirtualQueryEx and returns every 8-byte-aligned address where three consecutive doubles
// match (x, y, z) within `tolerance`. Feed it the coordinates shown on the F3 debug screen.
std::vector<uintptr_t> ScanForDoubleTriples(HANDLE process, double x, double y, double z,
                                            double tolerance, size_t maxResults = 200);
