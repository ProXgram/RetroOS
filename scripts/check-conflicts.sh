#!/usr/bin/env sh
set -eu

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "check-conflicts.sh must be run inside a Git repository" >&2
    exit 1
fi

REPO_ROOT=$(git rev-parse --show-toplevel)
cd "$REPO_ROOT"

PATTERN='^(<<<<<<<|=======|>>>>>>>)'
# Limit to source files to avoid flagging documentation snippets that mention the markers.
SEARCH_PATHS="bootloader kernel Makefile"

conflicts=$(git grep -n -E "$PATTERN" -- $SEARCH_PATHS 2>/dev/null || true)

if [ -n "$conflicts" ]; then
    cat <<'MSG' >&2
Error: Git conflict markers were detected in the source tree.
Please resolve the conflicts listed below before building (see the README section on resolving conflicts).
MSG
    printf '%s\n' "$conflicts" >&2
    exit 1
fi
