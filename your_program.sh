#!/bin/sh
#
# Use this script to run your program LOCALLY.
#
# Note: Changing this script WILL NOT affect how CodeCrafters runs your program.
#
# Learn more: https://codecrafters.io/program-interface

set -e # Exit early if any commands fail

# Navigate to the script's directory to ensure relative paths work correctly
cd "$(dirname "$0")"

# Compile the program using the updated compile.sh script
./.codecrafters/compile.sh

# Execute the compiled binary with any passed arguments
exec /tmp/codecrafters-build-redis-c "$@"
