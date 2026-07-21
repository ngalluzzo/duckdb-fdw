#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 && "$#" -ne 3 ]]; then
    echo "usage: verify-loadable-inventory.sh PATH_TO_EXTENSION [PINS transport|curl-free]" >&2
    exit 2
fi

readonly ARTIFACT="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
if [[ ! -f "${ARTIFACT}" ]]; then
    echo "extension artifact does not exist: ${ARTIFACT}" >&2
    exit 1
fi

case "$(uname -s)" in
    Darwin)
        exported="$(nm -gU "${ARTIFACT}")"
        expected='^[[:xdigit:]]+ T _duckdb_api_duckdb_cpp_init$'
        ;;
    Linux)
        exported="$(nm -D --defined-only "${ARTIFACT}")"
        expected='^[[:xdigit:]]+ T duckdb_api_duckdb_cpp_init$'
        ;;
    *)
        echo "unsupported inventory host: $(uname -s)" >&2
        exit 1
        ;;
esac

if [[ "$(printf '%s\n' "${exported}" | sed '/^[[:space:]]*$/d' | wc -l | tr -d ' ')" -ne 1 ]] ||
   ! printf '%s\n' "${exported}" | grep -Eq "${expected}"; then
    echo "loadable artifact exported an unexpected public symbol inventory:" >&2
    printf '%s\n' "${exported}" >&2
    exit 1
fi

readonly FORBIDDEN='127\.0\.0\.1|BuildControlledHttpRuntime|BuildControlledProductComposition|BuildControlledRuntimeScenario|ControlledRuntimeScenario|BuildLoopbackCurlRuntime|BuildDistinctSchemaConnectorCatalogFixture|ScanPlanTestAccess|ConnectorCatalogTestAccess|QueryRuntimeScenario|ControlledSocketMode|controlled_duckdb_api|duckdb_api_controlled|DUCKDB_API_CONTROLLED_PORT|duckdb_api_private_curl_option_observer_v1|FixtureScenario|SECRET_(MALFORMED|TYPE_MISMATCH)|duckdb_api_(stats|test|auth_adapter_test|secret_test)|fixture_secret|runtime_generated_|top-secret|test-only-(fixture|redacted)|DUCKDB_API_FIXTURE_SCENARIO|DUCKDB_API_CONNECTOR_PATH|DUCKDB_API_LIVE_PROOF_AUTHORITY'
if strings "${ARTIFACT}" | grep -E "${FORBIDDEN}" >/dev/null; then
    echo "loadable artifact contains a forbidden test-control identifier" >&2
    exit 1
fi

if [[ "$#" -eq 3 ]]; then
    readonly PINS="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
    readonly LINKAGE_CLASS="$3"
    python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
        linkage "${PINS}" "${LINKAGE_CLASS}" "${ARTIFACT}" >/dev/null
    echo "loadable artifact symbol, dependency, and test-control inventory passed"
else
    echo "loadable artifact public-symbol and test-control inventory passed"
fi
