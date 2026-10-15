@echo off
rem Builds mcradar-agent.jar (Java 8 bytecode; runs on every JVM from 8 up). Needs any JDK 8+ on PATH.
cd /d "%~dp0"
if exist out rmdir /s /q out
mkdir out
javac --release 8 -Xlint:-options -d out src\mcradar\*.java || exit /b 1
jar cfm mcradar-agent.jar manifest.mf -C out . || exit /b 1
echo built %cd%\mcradar-agent.jar
