// Wire.h - parser for the agent's UDP datagrams (the format is documented in agent/src/mcradar/Snapshot.java).
//
//   MCRADAR 3
//   S <status text>
//   C <x> <y> <z> <yaw> <pitch> <fov|-> <mode>                          the game camera (mode 1 first / 2 third-back / 3 third-front)
//   L <name> <x> <y> <z> <yaw|-> <pitch|-> <health|-> <w|-> <h|-> <uuid|->   local player
//   P <name> ...same columns...                                         other players
//   I <x> <y> <z> <w|-> <h|-> <count> <key> <ench|-> <tags|->           dropped item; key/ench/tags percent-encoded
// "MCRADAR 1" (yaw only) and "MCRADAR 2" (+ pitch, health) are still accepted. '-' = unknown.
#pragma once
#include "Types.h"

bool ParseDatagram(const char* data, size_t len, WorldSnapshot& out);
