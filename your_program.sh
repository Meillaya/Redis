#!/bin/sh
set -e # Exit on failure

# Run make to compile the program
make

# Execute the compiled binary
exec /tmp/codecrafters-build-redis-c "$@"
