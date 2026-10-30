#!/bin/sh
# Generate the build system from a fresh checkout.
set -e
mkdir -p m4 build-aux
autopoint --force
autoreconf --install --force
