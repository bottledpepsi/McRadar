// Wire.h - parser for the agent's UDP datagrams (see agent/src/mcradar/Snapshot.java).
//
//   MCRADAR 2
//   S <status text>
//   L <name> <x> <y> <z> <yaw|-> <pitch|-> <health|->     local player (optional)
//   P <name> <x> <y> <z> <yaw|-> <pitch|-> <health|->     other player, one line each
//   I <name> <count> <x> <y> <z>                          dropped item (reserved; not sent by the agent yet)
// "MCRADAR 1" (no pitch/health columns) is accepted too. '-' = unknown. Names never contain spaces
// (the agent replaces them with '_').
#pragma once
#include "Types.h"

bool ParseDatagram(const char* data, size_t len, WorldSnapshot& out);
