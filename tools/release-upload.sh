#!/bin/bash
# Attaches files to the GitHub Release of the pushed tag (.github/workflows/release.yml): the first
# job to finish creates the release (notes from changelog/$VERSION.md, else GitHub's), the others upload.
# Usage: release-upload.sh <file>...   Environment: GH_TOKEN, GITHUB_REF_NAME, GITHUB_REPOSITORY, VERSION.
set -euo pipefail
[ $# -gt 0 ] || { echo "no files"; exit 1; }
for asset in "$@"; do [ -f "$asset" ] || { echo "missing $asset"; exit 1; }; done
notes=(--generate-notes)
[ -f "changelog/$VERSION.md" ] && notes=(--notes-file "changelog/$VERSION.md")
if ! gh release view "$GITHUB_REF_NAME" --repo "$GITHUB_REPOSITORY" >/dev/null 2>&1; then
  gh release create "$GITHUB_REF_NAME" "$@" --repo "$GITHUB_REPOSITORY" --title "launchme $VERSION" --verify-tag "${notes[@]}" && exit 0
  # Created by the other job in the meantime: upload below.
fi
gh release upload "$GITHUB_REF_NAME" "$@" --repo "$GITHUB_REPOSITORY" --clobber
