#!/usr/bin/env bash

set -euo pipefail

# Ambient Python overrides can affect repository scripts reached through their
# shebang before an explicit `-I` flag is available. The developer cell owns
# its Python identity, so remove those inherited search/prefix controls here.
unset PYTHONHOME PYTHONPATH PYTHONSTARTUP PYTHONUSERBASE

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Environment bootstrap and build operations have different change drivers.
# Keep this file as the stable consumer-facing command boundary.
source "${REPOSITORY_ROOT}/scripts/lib/native-dev-environment.sh"
source "${REPOSITORY_ROOT}/scripts/lib/native-test-suite.sh"
source "${REPOSITORY_ROOT}/scripts/lib/native-dev-build.sh"

usage() {
    cat <<EOF
DuckDB API native developer commands

  make bootstrap                    prepare the pinned reusable developer cell
  make build PROFILE=debug|release  incrementally build the extension
  make test PROFILE=debug|release   run native, controlled, SQL, inventory, and demo oracles
  make demo PROFILE=debug|release   run the first query in a pinned clean host
  make paths PROFILE=debug|release  print exact developer host and artifact paths
  make verify PROFILE=debug|release run the fresh product test runner

PROFILE defaults to debug. DUCKDB_API_DEV_ROOT overrides the developer state root:
  ${DEFAULT_DEV_ROOT}

Developer artifacts are not release evidence. The tagged release gate remains
scripts/run-0.1-release-gate.sh with new build and evidence roots.
EOF
}

fail_usage() {
    echo "$1" >&2
    echo "usage: native-dev.sh help|bootstrap|build|test|demo|paths|verify [debug|release]" >&2
    exit 2
}

validate_profile() {
    case "$1" in
        debug | release)
            ;;
        *)
            fail_usage "profile must be debug or release: $1"
            ;;
    esac
}

readonly COMMAND="${1:-help}"
case "${COMMAND}" in
    help)
        [[ "$#" -eq 1 ]] || fail_usage "help takes no arguments"
        usage
        ;;
    bootstrap)
        [[ "$#" -eq 1 ]] || fail_usage "bootstrap takes no arguments"
        prepare_cell
        echo "native developer bootstrap passed"
        echo "pinned_python=${PINNED_PYTHON}"
        echo "developer_evidence=non-release"
        ;;
    build | test | demo | paths)
        [[ "$#" -le 2 ]] || fail_usage "${COMMAND} accepts at most one profile"
        profile="${2:-debug}"
        validate_profile "${profile}"
        run_build "${profile}"
        case "${COMMAND}" in
            test)
                run_tests
                echo "native developer tests passed"
                ;;
            demo)
                run_demo
                ;;
        esac
        print_paths
        ;;
    verify)
        [[ "$#" -le 2 ]] || fail_usage "verify accepts at most one profile"
        profile="${2:-debug}"
        validate_profile "${profile}"
        run_verify "${profile}"
        ;;
    *)
        fail_usage "unknown command: ${COMMAND}"
        ;;
esac
