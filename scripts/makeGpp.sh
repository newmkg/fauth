#!/bin/bash

set -e  # exit on error

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../" && pwd)"

echo "$SCRIPT_DIR"
echo "$PROJECT_ROOT"

cpp=g++
$cpp --version
includes="-I${PROJECT_ROOT}/include"
linkers="-levent -lcurl -lssl -lcrypto -lboost_json -lev"

(
    outputExe="fauth"
    main="$PROJECT_ROOT/src/main.cpp"

    if [ "$1" = "debug" ]; then
	debug_flags="-Wall -fexceptions -g -fno-omit-frame-pointer -rdynamic -std=c++23 -pg"
	debug_folder="$PROJECT_ROOT/build/Debug"
	rm -rf "$debug_folder"
	mkdir -p "$debug_folder"
	debugO="$debug_folder/$outputExe".o
	###
	echo "creating .o file for Debug..."
	$cpp $debug_flags $includes -c "$main" -o "$debugO"
	echo "creating debug exe file..."
	(
	    cd "$debug_folder" || exit
	    $cpp -o "$outputExe".out "$outputExe".o -pg $linkers
	    rm -f ./*.o
	)
    else
	release_flags="-fexceptions -O2 -std=c++23"
	release_folder="$PROJECT_ROOT/build/Release"
	rm -rf "$release_folder"
	mkdir -p "$release_folder"
	releaseO="$release_folder/$outputExe".o
	###
	echo "creating .o file for Release..."
	$cpp $release_flags $includes -c "$main" -o "$releaseO"
	echo "creating release exe file..."
	cd "$release_folder" || exit
	(
	    $cpp -o "$outputExe".out "$outputExe".o $linkers
	    rm -f ./*.o
	)
    fi
)
