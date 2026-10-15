cmake_minimum_required(VERSION 3.16)
project(McRadar LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "McRadar is a Windows-only project (Win32 API).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mcradar
  src/main.cpp src/Config.cpp src/ProcessManager.cpp src/MemoryScanner.cpp
  src/GameReader.cpp src/Poller.cpp src/RadarWindow.cpp
  src/AgentReceiver.cpp src/AgentLauncher.cpp)

# Console subsystem on purpose: log output appears in the terminal. Do NOT add -mwindows.
target_link_libraries(mcradar PRIVATE gdi32 user32 kernel32 ws2_32)

if(MINGW)
  target_compile_options(mcradar PRIVATE -Wall -Wextra -O2)
  option(MCRADAR_STATIC_RUNTIME "Link libgcc/libstdc++ statically" ON)
  if(MCRADAR_STATIC_RUNTIME)
    target_link_options(mcradar PRIVATE -static-libgcc -static-libstdc++)
  endif()
elseif(MSVC)
  target_compile_options(mcradar PRIVATE /W4 /permissive- /EHsc)
endif()

# Ship the config next to the exe.
add_custom_command(TARGET mcradar POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/radar.ini $<TARGET_FILE_DIR:mcradar>/radar.ini)

# Ship the Java agent next to the exe (build it first with agent/build.bat or agent/build.sh).
if(EXISTS ${CMAKE_SOURCE_DIR}/agent/mcradar-agent.jar)
  add_custom_command(TARGET mcradar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/agent/mcradar-agent.jar $<TARGET_FILE_DIR:mcradar>/mcradar-agent.jar)
endif()
