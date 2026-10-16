#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

# Linux owns source edits; the Mac owns its build directories and probe output.
# Excluded directories are also protected from rsync's deletion pass.
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
host=${1:-macos}
rsync -a --delete-delay \
  --exclude='/work/' --exclude='/build/' --exclude='/build-*/' \
  --exclude='/.wine/' --exclude='/.ghidra/' --exclude='/.direnv/' \
  --exclude='/.local-history/' \
  --exclude='/.venv/' --exclude='/result' --exclude='/result-*/' \
  --exclude='__pycache__/' --exclude='.pytest_cache/' \
  "$root/" "$host:Developer/bumpkins/"

# Copy the prepared, user-owned asset directory separately, preserving any
# local saves/configuration on the Mac. Never mirror all of work/.
if [[ -d "$root/work/patched" ]]; then
  rsync -a --exclude='*.[sS][aA][vV]' --exclude='bumpkins.ini' \
    "$root/work/patched/" "$host:Developer/bumpkins/work/patched/"
  rsync -a --ignore-existing --include='*/' --include='*.[sS][aA][vV]' \
    --include='bumpkins.ini' --exclude='*' \
    "$root/work/patched/" "$host:Developer/bumpkins/work/patched/"
fi
