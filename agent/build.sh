#!/bin/sh
# Builds esp-agent.jar (Java 8 bytecode; runs on every JVM from 8 up). Needs any JDK 8+.
set -e
cd "$(dirname "$0")"
rm -rf out && mkdir out
javac --release 8 -Xlint:-options -d out src/mcradar/*.java
jar cfm esp-agent.jar manifest.mf -C out .
echo "built $(pwd)/esp-agent.jar"
