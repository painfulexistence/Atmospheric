#!/bin/bash
# Move to project root and run core script in Debug mode
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR/.."
./scripts/buildWasm.sh debug "$@"
