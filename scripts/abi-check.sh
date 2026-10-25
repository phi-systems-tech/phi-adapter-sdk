#!/bin/sh
# Compares the built library with the committed ABI baseline and fails on any
# change an adapter built against the baseline release would notice: a removed
# or changed symbol, a public type whose layout moved, a vtable whose slots
# shifted. Additions are reported and allowed - that is what the d-pointer
# shape is for. No baseline yet means nothing to compare against, which is the
# state between the decision and the first release of the new shape.
#
# usage: abi-check.sh <baseline.abi> <libphi_adapter_sdk.so> <include dir> [<generated include dir>]
set -eu

baseline="$1"
library="$2"
include="$3"
generated="${4:-}"

if [ ! -f "$baseline" ]; then
    echo "abi-check: no baseline at $baseline; nothing to compare (taken at the first release of this shape)"
    exit 0
fi

headers="--headers-dir2 $include"
if [ -n "$generated" ]; then
    headers="$headers --headers-dir2 $generated"
fi

# abidiff exit bits: 1 error, 2 usage, 4 ABI change, 8 incompatible change.
# Compatible additions (bit 4 alone) pass; anything incompatible fails.
set +e
# shellcheck disable=SC2086
abidiff --no-added-syms --no-unreferenced-symbols --drop-private-types $headers "$baseline" "$library"
status=$?
set -e

if [ $((status & 8)) -ne 0 ] || [ $((status & 3)) -ne 0 ]; then
    echo "abi-check: FAILED - $library breaks the ABI of the baseline $baseline"
    exit 1
fi
echo "abi-check: $library is compatible with the baseline"
