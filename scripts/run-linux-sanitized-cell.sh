#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: run-linux-sanitized-cell.sh NEW_OUTPUT_ROOT_INSIDE_REPOSITORY" >&2
    exit 2
fi

readonly CALLER_REPOSITORY="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${CALLER_REPOSITORY}/scripts/lib/release-common.sh"
for command in docker git python3 sha256sum; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "the authoritative sanitizer launcher requires ${command}" >&2
        exit 1
    fi
done
readonly OUTPUT_ROOT="$(release_resolve_path "$1")"
readonly IMAGE="$(release_pin "${CALLER_REPOSITORY}" sanitizer_cell base_image)"
readonly PLATFORM="$(release_pin "${CALLER_REPOSITORY}" sanitizer_cell platform)"
readonly IMAGE_DIGEST="${IMAGE##*@}"

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "the authoritative sanitizer launcher requires a native Linux x86_64 host" >&2
    exit 1
fi
release_require_new_root "sanitizer output root" "${OUTPUT_ROOT}"
case "${OUTPUT_ROOT}" in
    "${CALLER_REPOSITORY}/.build/"*) ;;
    *)
        echo "sanitizer output root must be inside the repository's ignored build area" >&2
        exit 1
        ;;
esac
"${CALLER_REPOSITORY}/scripts/release-source-guard.sh" "${OUTPUT_ROOT}"

readonly SOURCE_COMMIT="$(git -C "${CALLER_REPOSITORY}" rev-parse HEAD)"
readonly SOURCE_TREE="$(git -C "${CALLER_REPOSITORY}" rev-parse 'HEAD^{tree}')"
readonly SNAPSHOT_PARENT="$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-sanitizer-source.XXXXXX")"
readonly SOURCE_SNAPSHOT="${SNAPSHOT_PARENT}/source"
readonly INSPECT_RECORD="${SNAPSHOT_PARENT}/docker-image-inspect.json"
readonly DAEMON_RECORD="${SNAPSHOT_PARENT}/docker-daemon-info.json"
readonly POST_DAEMON_RECORD="${SNAPSHOT_PARENT}/docker-daemon-info-after.json"
trap 'rm -rf "${SNAPSHOT_PARENT}"' EXIT
release_materialize_snapshot "${CALLER_REPOSITORY}" "${SOURCE_COMMIT}" "${SOURCE_SNAPSHOT}"
if [[ "$(git -C "${SOURCE_SNAPSHOT}" rev-parse 'HEAD^{tree}')" != "${SOURCE_TREE}" ]]; then
    echo "sanitizer source snapshot tree drifted" >&2
    exit 1
fi
"${SOURCE_SNAPSHOT}/scripts/release-source-guard.sh" "${SNAPSHOT_PARENT}/unused-build"

if [[ -n "${DOCKER_HOST:-}" || -n "${DOCKER_CONTEXT:-}" ]]; then
    echo "authoritative sanitizer evidence rejects Docker endpoint overrides" >&2
    exit 1
fi
readonly DOCKER_CONTEXT_NAME="$(docker context show)"
if [[ "${DOCKER_CONTEXT_NAME}" != "default" ]]; then
    echo "authoritative sanitizer evidence requires the default Docker context" >&2
    exit 1
fi
readonly DOCKER_ENDPOINT="$(docker context inspect --format \
    '{{(index .Endpoints "docker").Host}}' "${DOCKER_CONTEXT_NAME}")"
case "${DOCKER_ENDPOINT}" in
    unix://*) ;;
    *)
        echo "authoritative sanitizer evidence requires a local Unix-socket Docker daemon" >&2
        exit 1
        ;;
esac
docker --host "${DOCKER_ENDPOINT}" info --format '{{json .}}' >"${DAEMON_RECORD}"
IFS=$'\t' read -r DAEMON_ID DAEMON_OS DAEMON_ARCHITECTURE <<< \
    "$(release_docker_daemon_identity "${DAEMON_RECORD}")"
readonly DAEMON_ID DAEMON_OS DAEMON_ARCHITECTURE
if [[ -z "${DAEMON_ID}" || "${DAEMON_OS}" != "linux" || "${DAEMON_ARCHITECTURE}" != "x86_64" ]]; then
    echo "authoritative sanitizer evidence requires a Linux x86_64 Docker daemon" >&2
    exit 1
fi

docker --host "${DOCKER_ENDPOINT}" pull --platform "${PLATFORM}" "${IMAGE}" >/dev/null
docker --host "${DOCKER_ENDPOINT}" image inspect "${IMAGE}" >"${INSPECT_RECORD}"
readonly REPO_DIGESTS="$(docker --host "${DOCKER_ENDPOINT}" image inspect \
    --format '{{json .RepoDigests}}' "${IMAGE}")"
if [[ "${REPO_DIGESTS}" != *"${IMAGE_DIGEST}"* ]]; then
    echo "Docker did not resolve the pinned sanitizer image digest" >&2
    exit 1
fi

readonly OUTPUT_PARENT="$(dirname "${OUTPUT_ROOT}")"
readonly OUTPUT_NAME="$(basename "${OUTPUT_ROOT}")"
readonly HOST_UID="$(id -u)"
readonly HOST_GID="$(id -g)"
mkdir -p "${OUTPUT_PARENT}"
docker --host "${DOCKER_ENDPOINT}" run --rm --platform "${PLATFORM}" \
    --mount "type=bind,src=${SOURCE_SNAPSHOT},dst=/repo,readonly" \
    --mount "type=bind,src=${OUTPUT_PARENT},dst=/evidence" \
    -w /repo \
    "${IMAGE}" \
    bash -euxo pipefail -c '
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            binutils build-essential ca-certificates clang clang-format curl git make python3 rsync unzip
        git config --global --add safe.directory /repo
        export GIT_OPTIONAL_LOCKS=0
        scripts/run-linux-sanitized.sh /tmp/duckdb-api-build "/evidence/$1"
        chown -R "$2:$3" "/evidence/$1"
    ' bash "${OUTPUT_NAME}" "${HOST_UID}" "${HOST_GID}"

docker --host "${DOCKER_ENDPOINT}" info --format '{{json .}}' >"${POST_DAEMON_RECORD}"
IFS=$'\t' read -r POST_DAEMON_ID POST_DAEMON_OS POST_DAEMON_ARCHITECTURE <<< \
    "$(release_docker_daemon_identity "${POST_DAEMON_RECORD}")"
readonly POST_DAEMON_ID POST_DAEMON_OS POST_DAEMON_ARCHITECTURE
if [[ "${POST_DAEMON_ID}" != "${DAEMON_ID}" || "${POST_DAEMON_OS}" != "${DAEMON_OS}" ||
      "${POST_DAEMON_ARCHITECTURE}" != "${DAEMON_ARCHITECTURE}" ]]; then
    echo "Docker daemon identity changed during sanitizer execution" >&2
    exit 1
fi
"${SOURCE_SNAPSHOT}/scripts/release-source-guard.sh" "${SNAPSHOT_PARENT}/final-source-guard"
python3 -I "${SOURCE_SNAPSHOT}/scripts/verify-sanitizer-manifest.py" \
    "${SOURCE_SNAPSHOT}" "${OUTPUT_ROOT}/manifest.json" "${OUTPUT_ROOT}/manifest.sha256" \
    "${OUTPUT_ROOT}/duckdb_api.duckdb_extension" "${OUTPUT_ROOT}/compile_commands.json" \
    "${OUTPUT_ROOT}/sanitizer-flags.txt"
python3 -I "${SOURCE_SNAPSHOT}/scripts/write-sanitizer-envelope.py" \
    "${SOURCE_SNAPSHOT}" "${OUTPUT_ROOT}" "${INSPECT_RECORD}" "${DAEMON_RECORD}" \
    "${DOCKER_CONTEXT_NAME}" "${DOCKER_ENDPOINT}" "${OUTPUT_ROOT}/envelope.json"
sha256sum "${OUTPUT_ROOT}/envelope.json" >"${OUTPUT_ROOT}/envelope.sha256"
chmod 0444 "${OUTPUT_ROOT}/envelope.json" "${OUTPUT_ROOT}/envelope.sha256"

echo "native Linux amd64 sanitizer cell completed"
echo "evidence=${OUTPUT_ROOT}"
echo "envelope=${OUTPUT_ROOT}/envelope.json"
