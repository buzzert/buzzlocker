#!/bin/bash

# TODO: Makefile? This will end up in the xsecurelock project anyway...
clang $(pkg-config --libs --cflags x11 cairo librsvg-2.0) -o buzzsaver buzzsaver.c

