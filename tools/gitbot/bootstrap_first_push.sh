#!/usr/bin/env bash
# One-time bootstrap: publish the Nomiq scaffold to GitHub the systematic way.
#   1. push the initial commit to main
#   2. create labels + issue #1
#   3. push the scaffold branch, open PR "Closes #1"
#   4. wait for CI, squash-merge, sync local main
# Usage (in the folder containing nomiq.bundle):  bash bootstrap_first_push.sh
set -euo pipefail
BUNDLE="${1:-nomiq.bundle}"
REPO="anand2532/Nomiq"
[[ -f "$BUNDLE" ]] || { echo "bundle $BUNDLE not found"; exit 1; }
command -v gh >/dev/null || { echo "install GitHub CLI: https://cli.github.com"; exit 1; }
[[ "$(gh api user --jq .login)" == "anand2532" ]] || { echo "run: gh auth login (as anand2532)"; exit 1; }
gh auth setup-git

git clone "$BUNDLE" Nomiq && cd Nomiq
git remote set-url origin "https://github.com/$REPO.git"
git fetch "../$BUNDLE" 'refs/heads/*:refs/remotes/bundle/*'
git checkout main
git push -u origin main                                   # initial commit only

tools/gitbot/gitflow.sh labels
url=$(gh issue create --repo "$REPO" --title "Project scaffold: docs, firmware skeleton, enclosure, MCP + engineer agents" \
      --label "phase-0" --label "area/docs" --label "area/infra" --assignee anand2532 \
      --body-file .github/bootstrap/issue-1.md)
n="${url##*/}"; echo "created issue #$n"

br="feat/${n}-project-scaffold"
git checkout -b "$br" bundle/feat/project-scaffold
git push -u origin "$br"
gh pr create --repo "$REPO" --base main --head "$br" --assignee anand2532 \
  --title "Project scaffold: docs, firmware skeleton, enclosure, MCP + engineer agents" \
  --body "Closes #${n}

$(cat .github/bootstrap/pr-1.md)"
tools/gitbot/gitflow.sh merge
echo "Done. Repo: https://github.com/$REPO"
