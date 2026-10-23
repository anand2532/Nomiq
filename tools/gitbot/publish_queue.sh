#!/usr/bin/env bash
# Publish queued work packages from an offline bundle to GitHub, one at a time:
#   for each WP:  ISSUE -> branch <type>/<issue#>-<slug> -> PR "Closes #n" -> CI -> squash merge
# Idempotent: already-merged WPs are skipped (tracked in published.txt and by issue title).
#
# Layout (the folder Claude delivers):
#   nomiq.bundle      git bundle with refs queue/NN-slug (stacked, in order)
#   queue.tsv         id <TAB> bundle-branch <TAB> type <TAB> area <TAB> phase <TAB> title
#   issues/NN.md      issue body for each WP
#   publish_queue.sh  this script
# Usage:  cd <that folder> && bash publish_queue.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="anand2532/Nomiq"; LOGIN="anand2532"
STATE="$HERE/published.txt"; touch "$STATE"

command -v gh >/dev/null || { echo "install GitHub CLI: https://cli.github.com"; exit 1; }
[[ "$(gh api user --jq .login)" == "$LOGIN" ]] || { echo "run: gh auth login   (as $LOGIN)"; exit 1; }
# Pushing .github/workflows/* needs the 'workflow' scope (gh's default login does not include it)
if gh api -i user 2>/dev/null | grep -qi '^x-oauth-scopes:' && ! gh api -i user | grep -i '^x-oauth-scopes:' | grep -qw workflow; then
  echo "Your gh token lacks the 'workflow' scope (needed to push CI files). Run:"
  echo "    gh auth refresh -h github.com -s workflow"
  echo "then re-run this script."
  exit 1
fi
gh auth setup-git >/dev/null

cd "$HERE"
[[ -d Nomiq/.git ]] || git clone "https://github.com/$REPO.git" Nomiq
cd Nomiq
uid=$(gh api user --jq .id)
git config user.name "$LOGIN"; git config user.email "${uid}+${LOGIN}@users.noreply.github.com"
git fetch -q origin
git fetch -q --force "$HERE/nomiq.bundle" 'refs/heads/queue/*:refs/remotes/bundle/queue/*'

# labels (once; --force updates)
for l in phase-0 phase-1 phase-2 phase-3 phase-4 phase-5 phase-6 phase-7 area/hardware area/pcb area/firmware \
         area/protocol area/audio-stt area/camera area/power area/mechanical area/test area/docs area/bom area/infra; do
  gh label create "$l" --repo "$REPO" --force >/dev/null 2>&1 || true
done

# Only these checks gate a merge; others (enclosure, firmware-build) are reported but non-blocking
BLOCKING="${NOMIQ_BLOCKING_CHECKS:-host-tests protocol-tests tools}"
wait_checks() {
  local br="$1" tries=0 out
  sleep 20
  while :; do
    out=$(gh pr checks "$br" --repo "$REPO" 2>&1 || true)
    if grep -qi "no checks reported" <<<"$out"; then echo "no CI checks"; return 0; fi
    local state
    state=$(awk -F'\t' -v b="$BLOCKING" 'BEGIN{n=split(b,a," ");for(i=1;i<=n;i++)B[a[i]]=1}
             ($1 in B){seen=1; if($2=="fail")f=1; else if($2=="pending")p=1}
             END{print (f?"fail":(p||!seen)?"pending":"pass")}' <<<"$out")
    if [[ "$state" == "fail" ]]; then echo "$out"; return 1; fi
    if [[ "$state" == "pass" ]]; then
      echo "$out" | awk -F'\t' '{printf "  %-16s %s\n", $1, $2}'
      grep -qP '\tfail\t' <<<"$out" && echo "  (non-blocking check failed — see the PR; merge continues)"
      return 0
    fi
    tries=$((tries + 1))
    if (( tries > 90 )); then echo "$out"; echo "blocking checks still pending after 30 min"; return 1; fi
    sleep 20
  done
}

slug() { echo "$1" | tr '[:upper:]' '[:lower:]' | sed -E 's/[^a-z0-9]+/-/g; s/^-|-$//g' | cut -c1-40 | sed -E 's/-+$//'; }
prev=""
while IFS=$'\t' read -r id qbr type area phase title; do
  [[ -z "$id" || "$id" == \#* ]] && continue
  tip="bundle/$qbr"
  git rev-parse -q --verify "$tip" >/dev/null || { echo "missing $qbr in bundle"; exit 1; }
  if grep -qx "$id" "$STATE"; then prev="$tip"; continue; fi
  echo "=== WP $id: $title"

  git checkout -q main 2>/dev/null || git checkout -q -b main origin/main
  git reset -q --hard origin/main && git pull -q --ff-only origin main

  # issue (reuse if it already exists)
  n=$(gh issue list --repo "$REPO" --state all --search "in:title \"$title\"" --json number,title \
        --jq "map(select(.title==\"$title\"))[0].number // empty")
  if [[ -z "$n" ]]; then
    url=$(gh issue create --repo "$REPO" --title "$title" --label "area/$area" --label "$phase" \
          --assignee "$LOGIN" --body-file "$HERE/issues/$id.md")
    n="${url##*/}"
  fi
  br="$type/$n-$(slug "$title")"

  state=$(gh pr list --repo "$REPO" --head "$br" --state all --json state --jq '.[0].state // empty')
  if [[ "$state" == "MERGED" ]]; then echo "$id" >> "$STATE"; prev="$tip"; continue; fi

  git checkout -q -B "$br" "$tip"
  if [[ -n "$prev" ]]; then git rebase -q --onto origin/main "$prev" "$br"; else git rebase -q origin/main "$br"; fi
  if ! git push -q -u origin "$br" --force-with-lease; then
    echo "push of $br failed. If the message mentions 'workflow', run: gh auth refresh -h github.com -s workflow"
    exit 1
  fi

  if [[ -z "$state" ]]; then
    gh pr create --repo "$REPO" --base main --head "$br" --title "$title" --assignee "$LOGIN" \
      --body "Closes #$n

$(cat "$HERE/issues/$id.md")

## Commits
$(git log --format='- %s' origin/main..HEAD)"
  fi

  if ! wait_checks "$br"; then echo "CI failed on $br — fix, then re-run this script"; exit 1; fi
  gh pr merge "$br" --repo "$REPO" --squash --delete-branch
  git fetch -q origin && git checkout -q main && git reset -q --hard origin/main
  echo "$id" >> "$STATE"
  prev="$tip"
  echo "merged WP $id as PR for issue #$n"
done < "$HERE/queue.tsv"
echo "All queued work packages published. https://github.com/$REPO"
