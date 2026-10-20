// Wire.h - parser for the agent's UDP datagrams (the format is documented in agent/src/mcradar/Snapshot.java).
//
//   MCRADAR 4                                                           (v4 = v3 + the T and F lines below)
//   T <nanoTime>                                                        v4: instant the agent read the game state (agent clock, ns)
//   S <status text>
//   C <x> <y> <z> <yaw> <pitch> <fov|-> <mode>                          the game camera (mode 1 first / 2 third-back / 3 third-front)
//   L <name> <x> <y> <z> <yaw|-> <pitch|-> <health|-> <w|-> <h|-> <uuid|->   local player
//   P <name> ...same columns...                                         other players
//   F 1                                                                 v4: the item list follows (omitted => keep the previous items)
//   I <x> <y> <z> <w|-> <h|-> <count> <key> <ench|-> <tags|->           dropped item; key/ench/tags percent-encoded
// "MCRADAR 1" (yaw only), "MCRADAR 2" (+ pitch, health) and "MCRADAR 3" are still accepted. '-' = unknown.
#pragma once
#include "Types.h"

bool ParseDatagram(const char* data, size_t len, WorldSnapshot& out);
