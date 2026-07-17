#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: run-linux-sanitized-cell.sh NEW_OUTPUT_ROOT_INSIDE_REPOSITORY" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for command in docker git python3; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "the authoritative sanitizer launcher requires ${command}" >&2
        exit 1
    fi
done
readonly OUTPUT_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$1")"
readonly IMAGE='docker.io/library/ubuntu:24.04@sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90'
readonly IMAGE_DIGEST="${IMAGE##*@}"

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "the authoritative sanitizer launcher requires a native Linux x86_64 host" >&2
    exit 1
fi
if [[ -e "${OUTPUT_ROOT}" ]]; then
    echo "sanitizer output root must not already exist: ${OUTPUT_ROOT}" >&2
    exit 1
fi
case "${OUTPUT_ROOT}" in
    "${REPOSITORY_ROOT}/.build/"*) ;;
    *)
        echo "sanitizer output root must be inside the repository's ignored build area" >&2
        exit 1
        ;;
esac
if [[ -n "$(git -C "${REPOSITORY_ROOT}" status --porcelain --untracked-files=all)" ]]; then
    echo "sanitizer source worktree is not clean" >&2
    exit 1
fi

readonly SOURCE_COMMIT="$(git -C "${REPOSITORY_ROOT}" rev-parse HEAD)"
readonly CONTAINER_OUTPUT="/repo/${OUTPUT_ROOT#"${REPOSITORY_ROOT}"/}"
docker pull "${IMAGE}" >/dev/null
readonly REPO_DIGESTS="$(docker image inspect --format '{{json .RepoDigests}}' "${IMAGE}")"
if [[ "${REPO_DIGESTS}" != *"${IMAGE_DIGEST}"* ]]; then
    echo "Docker did not resolve the pinned sanitizer image digest" >&2
    exit 1
fi

docker run --rm --platform linux/amd64 \
    -e "DUCKDB_API_VERIFIED_BASE_IMAGE=${IMAGE}" \
    -e "DUCKDB_API_SANITIZER_SOURCE_COMMIT=${SOURCE_COMMIT}" \
    -v "${REPOSITORY_ROOT}:/repo" \
    -w /repo \
    "${IMAGE}" \
    bash -euxo pipefail -c '
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            binutils build-essential ca-certificates clang clang-format curl git make python3 rsync unzip
        git config --global --add safe.directory /repo
        scripts/run-linux-sanitized.sh /tmp/duckdb-api-build "$1"
    ' bash "${CONTAINER_OUTPUT}"

echo "native Linux amd64 sanitizer cell completed"
echo "evidence=${OUTPUT_ROOT}"
