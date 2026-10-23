#!/usr/bin/env bash
# Nomiq git bot — enforces: ISSUE -> BRANCH -> COMMITS -> PR -> CHECKS -> MERGE -> SYNC
# All operations run as the GitHub account logged into `gh` (expected: anand2532).
#
#   tools/gitbot/gitflow.sh start  "<issue title>" [area] [phase] [body-file]
#   tools/gitbot/gitflow.sh commit "<type(scope): message>"
#   tools/gitbot/gitflow.sh pr     [--draft]
#   tools/gitbot/gitflow.sh merge
#   tools/gitbot/gitflow.sh sync
#   tools/gitbot/gitflow.sh status
#   tools/gitbot/gitflow.sh labels          # create standard labels once
set -euo pipefail

OWNER_LOGIN="${NOMIQ_GH_LOGIN:-anand2532}"
REPO="${NOMIQ_GH_REPO:-anand2532/Nomiq}"
MAIN="main"

die() { echo "gitbot: $*" >&2; exit 1; }
info() { echo "gitbot: $*"; }

preflight() {
  command -v gh >/dev/null || die "GitHub CLI 'gh' not installed"
  local who; who=$(gh api user --jq .login 2>/dev/null) || die "gh not authenticated — run: gh auth login"
  [[ "$who" == "$OWNER_LOGIN" ]] || die "gh is logged in as '$who', expected '$OWNER_LOGIN'"
  local id; id=$(gh api user --jq .id)
  git config user.name  "$OWNER_LOGIN"
  git config user.email "${id}+${OWNER_LOGIN}@users.noreply.github.com"
}

current_branch() { git rev-parse --abbrev-ref HEAD; }
issue_from_branch() { current_branch | sed -nE 's#^[a-z]+/([0-9]+)-.*#\1#p'; }
slugify() { echo "$1" | tr '[:upper:]' '[:lower:]' | sed -E 's/[^a-z0-9]+/-/g; s/^-|-$//g' | cut -c1-40 | sed -E 's/-+$//'; }

require_clean() { [[ -z "$(git status --porcelain)" ]] || die "working tree not clean — commit or stash first"; }

cmd_labels() {
  preflight
  for l in "phase-0:ededed" "phase-1:c5def5" "phase-2:c5def5" "phase-3:f9d0c4" "phase-4:c5def5" \
           "phase-5:c5def5" "phase-6:c5def5" "phase-7:d4c5f9" "area/hardware:0e8a16" "area/pcb:0e8a16" \
           "area/firmware:1d76db" "area/protocol:1d76db" "area/audio-stt:5319e7" "area/camera:5319e7" \
           "area/power:fbca04" "area/mechanical:b60205" "area/test:006b75" "area/docs:bfdadc" \
           "area/bom:c2e0c6" "area/infra:000000"; do
    gh label create "${l%%:*}" --color "${l##*:}" --repo "$REPO" --force >/dev/null && echo "label ${l%%:*}"
  done
}

cmd_start() {
  local title="${1:?issue title required}" area="${2:-docs}" phase="${3:-phase-0}" body_file="${4:-}"
  preflight; require_clean
  git checkout "$MAIN" && git pull --ff-only origin "$MAIN"
  local body="Created by gitbot.\n\n## Goal\n${title}\n\n## Acceptance criteria\n- [ ] \n\n## Requirements\nREQ-"
  local args=(--repo "$REPO" --title "$title" --label "area/$area" --label "$phase" --assignee "$OWNER_LOGIN")
  local url
  if [[ -n "$body_file" ]]; then url=$(gh issue create "${args[@]}" --body-file "$body_file")
  else url=$(gh issue create "${args[@]}" --body "$(printf "$body")"); fi
  local n="${url##*/}"
  local prefix="feat"; [[ "$title" =~ ^(fix|docs|chore|test|refactor) ]] && prefix="${BASH_REMATCH[1]}"
  local br="${prefix}/${n}-$(slugify "$title")"
  git checkout -b "$br"
  info "issue #$n -> branch $br"
}

cmd_commit() {
  local msg="${1:?commit message required (conventional commit)}"
  preflight
  [[ "$(current_branch)" != "$MAIN" ]] || die "never commit directly to $MAIN — run 'start' first"
  [[ "$msg" =~ ^(feat|fix|docs|chore|test|refactor|perf|build|ci|hw|mech)(\(.+\))?:\ .+ ]] \
    || die "use conventional commits, e.g. 'feat(proto): add SACK window'"
  local n; n=$(issue_from_branch)
  git add -A
  git diff --cached --quiet && die "nothing to commit"
  git commit -m "$msg" -m "Refs #${n}"
}

cmd_pr() {
  preflight
  local br n; br=$(current_branch); n=$(issue_from_branch)
  [[ "$br" != "$MAIN" && -n "$n" ]] || die "not on an issue branch"
  require_clean
  git fetch origin "$MAIN" && git rebase "origin/$MAIN"
  git push -u origin "$br" --force-with-lease
  local title; title=$(gh issue view "$n" --repo "$REPO" --json title --jq .title)
  if gh pr view "$br" --repo "$REPO" >/dev/null 2>&1; then info "PR already exists"; gh pr view "$br" --repo "$REPO" --web=false; return; fi
  gh pr create --repo "$REPO" --base "$MAIN" --head "$br" --title "$title" --assignee "$OWNER_LOGIN" ${1:-} \
    --body "Closes #${n}

## Changes
$(git log --format='- %s' "origin/$MAIN..HEAD")

## Checklist
- [ ] Requirements (REQ-IDs) referenced
- [ ] Protocol host tests pass (if firmware/proto touched)
- [ ] ERC/DRC attached (if hardware touched)
- [ ] Docs updated"
}

cmd_merge() {
  preflight
  local br; br=$(current_branch)
  sleep 20  # give CI a moment to register
  local out
  if ! out=$(gh pr checks "$br" --repo "$REPO" --watch --fail-fast 2>&1); then
    if grep -qi "no checks reported" <<<"$out"; then info "no CI checks on this PR"
    else echo "$out"; die "checks failed — fix before merging"; fi
  fi
  gh pr merge "$br" --repo "$REPO" --squash --delete-branch
  git checkout "$MAIN" && git pull --ff-only origin "$MAIN"
  git branch -D "$br" 2>/dev/null || true
  info "merged $br into $MAIN"
}

cmd_sync() {
  preflight
  git fetch --prune origin
  local br; br=$(current_branch)
  if [[ "$br" == "$MAIN" ]]; then git pull --ff-only origin "$MAIN"; else git rebase "origin/$MAIN"; fi
}

cmd_status() {
  preflight
  echo "branch: $(current_branch)   issue: #$(issue_from_branch)"
  git status -sb
  gh pr status --repo "$REPO" || true
}

case "${1:-}" in
  start) shift; cmd_start "$@";;
  commit) shift; cmd_commit "$@";;
  pr) shift; cmd_pr "$@";;
  merge) cmd_merge;;
  sync) cmd_sync;;
  status) cmd_status;;
  labels) cmd_labels;;
  *) sed -n '2,12p' "$0"; exit 1;;
esac
