@echo off
rem One-step build for Windows.  Output: dist\mcradar.exe + mcradar-agent.jar + radar.ini
rem Needs: a JDK (javac/jar on PATH) and a 64-bit MinGW-w64 g++ with POSIX threads.
rem g++ is searched on PATH, then in CLion's bundled MinGW, then in MSYS2.
setlocal
cd /d "%~dp0"
set "ROOT=%cd%"

call agent\build.bat || exit /b 1
cd /d "%ROOT%"

where g++ >nul 2>nul
if not errorlevel 1 goto have_gxx

set "GXXDIR="
for /d %%D in ("%ProgramFiles%\JetBrains\CLion*") do if exist "%%~D\bin\mingw\bin\g++.exe" set "GXXDIR=%%~D\bin\mingw\bin"
for /d %%D in ("%LOCALAPPDATA%\Programs\CLion*") do if exist "%%~D\bin\mingw\bin\g++.exe" set "GXXDIR=%%~D\bin\mingw\bin"
for /d %%D in ("%LOCALAPPDATA%\JetBrains\Toolbox\apps\CLion\ch-0\*") do if exist "%%~D\bin\mingw\bin\g++.exe" set "GXXDIR=%%~D\bin\mingw\bin"
if not defined GXXDIR if exist "C:\msys64\ucrt64\bin\g++.exe" set "GXXDIR=C:\msys64\ucrt64\bin"
if not defined GXXDIR if exist "C:\msys64\mingw64\bin\g++.exe" set "GXXDIR=C:\msys64\mingw64\bin"
if not defined GXXDIR (
  echo.
  echo ERROR: g++ not found. Either build from CLion ^(open this folder, it uses CMakeLists.txt^),
  echo or add a MinGW-w64 "bin" folder to PATH, e.g. CLion's:
  echo   C:\Program Files\JetBrains\CLion ^<version^>\bin\mingw\bin
  exit /b 1
)
set "PATH=%GXXDIR%;%PATH%"
echo Using g++ from %GXXDIR%

:have_gxx
g++ -v 2>&1 | findstr /c:"Thread model: posix" >nul || echo WARNING: this g++ does not report "Thread model: posix"; std::jthread may fail to compile.

if not exist dist mkdir dist
g++ -std=c++20 -O2 -Wall -Wextra src\*.cpp -o dist\mcradar.exe -lgdi32 -luser32 -lws2_32 -static-libgcc -static-libstdc++ || exit /b 1
copy /y agent\mcradar-agent.jar dist\ >nul
copy /y radar.ini dist\ >nul
echo.
echo Done: %ROOT%\dist\mcradar.exe
echo Run it from the dist folder:  cd dist ^&^& mcradar.exe --demo
