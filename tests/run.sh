#!/bin/sh
# Convenience wrapper: build and run the host tests.
cd "$(dirname "$0")" && exec make "$@"
