#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail

declare -r BRANCH="$1"
declare -r PR_NUMBER="$2"
declare -r MERGE_COMMIT_SHA="$3"

git config user.name "${GITHUB_ACTOR}"
git config user.email "${GITHUB_ACTOR}@users.noreply.github.com"

NEW_BRANCH="automerge-to-${BRANCH}-pr${PR_NUMBER}-$(date +%s)"
declare -r NEW_BRANCH
echo "+++ Create local branch ${NEW_BRANCH} for PR #${PR_NUMBER} at ${MERGE_COMMIT_SHA}"
git checkout -b "${NEW_BRANCH}" "${MERGE_COMMIT_SHA}"

echo "+++ Try merging ${MERGE_COMMIT_SHA} onto ${BRANCH}"
git checkout "${BRANCH}"
set +o errexit
git merge --no-ff "${NEW_BRANCH}"
set -o errexit
if [[ -z $(git status --porcelain) ]]; then
  echo "+++ Merged cleanly, push to GitHub."
  git push origin "${BRANCH}"
  gh pr comment "${PR_NUMBER}" --body "Automated merge to ${BRANCH} was **clean** :tada:. Please check the build status."
  exit 0
fi

echo "+++ Merge failed, create a PR to resolve."
git merge --abort
git checkout "${NEW_BRANCH}"
git push origin "${NEW_BRANCH}"

fix_pr_url=$(gh pr create \
  --title "Failed automated merge of #${PR_NUMBER}." \
  --body "Failed automated merge to ${BRANCH} triggered by #${PR_NUMBER}. Please resolve the conflicts and push manually, see [C Release Instructions](https://github.com/nats-io/nats-internal/blob/master/release-instructions/C.md)" \
  --head "${NEW_BRANCH}" \
  --base "${BRANCH}")

gh pr comment "${PR_NUMBER}" --body "Automated merge to ${BRANCH} **failed** :x:. Fix PR ${fix_pr_url} filed."
