#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p .claude/skills/honor-aap-an00-root .cursor/skills/honor-aap-an00-root
cp honor-aap-an00-root/SKILL.md honor-aap-an00-root/reference.md \
  .claude/skills/honor-aap-an00-root/
cp honor-aap-an00-root/SKILL.md .cursor/skills/honor-aap-an00-root/SKILL.md
echo "mirrors synced"
