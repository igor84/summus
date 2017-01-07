#!/bin/bash

mkdir -p bin
clang++ -std=c11 `llvm-config --cflags` -x c compiler/*.c utility/*.c `llvm-config --ldflags --libs core analysis native bitwriter --system-libs` -lm -o bin/summus
