#!/bin/sh
# Writes the ABI baseline from the built library. A release step, taken on
# purpose: the dump is what abi-check.sh holds every later build against, so
# rewriting it says "this is the shape adapters may now be built against".
# Private types (the d-pointer Impl structs) are left out; they are the part
# that is allowed to change.
#
# usage: abi-baseline.sh <baseline.abi> <libphi_adapter_sdk.so> <include dir> [<generated include dir>]
set -eu

baseline="$1"
library="$2"
include="$3"
generated="${4:-}"

headers="--headers-dir $include"
if [ -n "$generated" ]; then
    headers="$headers --headers-dir $generated"
fi

mkdir -p "$(dirname "$baseline")"
# shellcheck disable=SC2086
abidw --drop-private-types --no-corpus-path $headers --out-file "$baseline" "$library"
echo "abi-baseline: wrote $baseline from $library"
