#!/bin/bash

set -e  # exit on error

export socketPath="/tmp/myUtils/fauth"
mkdir -p "$(dirname "$socketPath")"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../" && pwd)"

RUN_FILE="fauth"
(
    if [ "$1" = "debug" ]; then
	exec "$PROJECT_ROOT/build/Debug/${RUN_FILE}.out"
    else
	exec "$PROJECT_ROOT/build/Release/${RUN_FILE}.out"
    fi
)
