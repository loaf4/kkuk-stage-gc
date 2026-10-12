#!/usr/bin/env bash
# Regenerates the C code of the Stella test programs with the pinned compiler.
# Needs Docker. The generated .c files are committed, so the tests themselves
# run without it.
set -eu
cd "$(dirname "$0")"

IMAGE=fizruk/stella
DIGEST=sha256:86d6123aff226787a02b1b8021c7d4f2cd3703586155f5a96d6f0a7c21d805c6

for source in *.stella; do
  docker run --rm -i "$IMAGE@$DIGEST" compile /dev/stdin -o /dev/stdout \
    < "$source" > "${source%.stella}.c"
  echo "compiled $source"
done
