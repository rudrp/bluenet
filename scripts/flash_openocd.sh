#!/bin/bash

cmd=${1:? "Usage: $0 \"cmd\", \"target\""}

if [[ $cmd != "help" && $cmd != "init" && $cmd != "combined" && $cmd != "connect" ]]; then
	target=${2:? "Usage: $0 \"cmd\", \"target\""}
fi

source ../CMakeBuild.config

debug_target=$target.elf
binary_target=$target
combined_target=combined.hex

COMPILER_PATH=/opt/compiler/gcc-arm-none-eabi-4_8-2014q3/bin

init() {
	openocd -d3 -f openocd/openocd.cfg
}

connect() {
	telnet 127.0.0.1 4444
}

upload() {
	cd telnet 
	./flash.expect app $binary_target $APPLICATION_START_ADDRESS
}

# Flash application and firmware as combined binary
combined() {
	cd telnet
	./flash.expect all $combined_target 0
}

debug() {
	$COMPILER_PATH/arm-none-eabi-gdb -tui ../build/$debug_target
}

help() {
	echo $"Usage: $0 {init|connect|upload|combined|debug}"
}

case "$cmd" in 
	init)
		init
		;;
	connect)
		connect
		;;
	upload)
		upload
		;;
	combined)
		combined
		;;
	debug)
		debug
		;;
	help)
		help
		;;
	*)
		echo $"Usage: $0 {init|connect|upload|combined|debug}"
		exit 1
esac


