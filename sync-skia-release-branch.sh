#!/usr/bin/env bash
# Usage: ./sync-skia-release-branch.sh 151   (fetches chrome/m151 from Skia and pushes it to the fork)
set -e

git remote add upstream https://github.com/google/skia.git 2>/dev/null || true

git fetch upstream "chrome/m$1"
git push origin "refs/remotes/upstream/chrome/m$1:refs/heads/chrome/m$1"