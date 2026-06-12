#!/bin/sh
set -e
cd "$(dirname "$0")/.."

cmake -S harness -B harness/build
cmake --build harness/build
mkdir -p harness/out
./harness/build/render_harness "$@"
