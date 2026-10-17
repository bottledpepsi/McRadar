#!/bin/sh
# Host-side unit tests (Linux/macOS/MSYS2): config <-> JSON, C# config compatibility, friends, profiles,
# wire parser, camera projection (orientation at 4 yaws, pitch, FOV edge), smoother.
set -e
cd "$(dirname "$0")"
S=../src
g++ -std=c++20 -O1 -Wall -Wextra -I$S unit_tests.cpp $S/Json.cpp $S/Presets.cpp $S/EspConfig.cpp \
    $S/FriendList.cpp $S/ProfileManager.cpp $S/Wire.cpp $S/Logger.cpp -o /tmp/mcesp_unit_tests
mkdir -p /tmp/mcesp_ut && cd /tmp/mcesp_ut && /tmp/mcesp_unit_tests | grep -E "FAIL|ALL OK|FAILURES"
