#!/bin/bash

set -x

# 获取当前目录路径
WORKDIR=$(pwd)

rm -rf "$WORKDIR/build"/*

mkdir -p "$WORKDIR/build"
cd "$WORKDIR/build" &&
	cmake .. &&
	make -j$(nproc)
