#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly WORKFLOW="${REPOSITORY_ROOT}/.github/workflows/linux-amd64-sanitized.yml"

ruby - "${WORKFLOW}" <<'RUBY'
require "yaml"

def lines(step)
  step.fetch("run").lines.map(&:strip).reject(&:empty?)
end

def validate(workflow)
  steps = workflow.fetch("jobs").fetch("sanitizer").fetch("steps")
  expected_order = [
    "Verify and materialize the immutable release source",
    "Run in the pinned Linux amd64 cell",
    "Stage a visible exact evidence allowlist",
    "Upload sanitizer evidence for release binding",
    "Download the retained sanitizer evidence",
    "Verify the downloaded evidence bytes",
  ]
  named = steps.map { |step| step["name"] }.compact
  positions = expected_order.map { |name| named.index(name) }
  raise "sanitizer custody steps are missing" if positions.any?(&:nil?)
  raise "sanitizer custody steps are out of order" unless positions == positions.sort

  steps.each do |step|
    next unless step["uses"]
    raise "workflow action is not pinned to a full commit: #{step["uses"]}" unless \
      step["uses"].match?(/\A[^@]+@[0-9a-f]{40}\z/)
  end

  relevant = expected_order.to_h do |name|
    [name, steps.find { |step| step["name"] == name }]
  end
  relevant.each do |name, step|
    raise "custody step is conditionally disabled: #{name}" if step.key?("if")
  end

  raise "immutable materialization command drifted" unless lines(relevant.fetch(expected_order[0])) == [
    'python3 -I experiments/repeatable-installation/enablement/verify_trial_trust.py "$PWD"',
    'git worktree add --detach "${RUNNER_TEMP}/duckdb-api-v0.1.0" f855dfb5f5de0be7cb8ffd6a58d54552aeaada8d',
    'test "$(git -C "${RUNNER_TEMP}/duckdb-api-v0.1.0" rev-parse HEAD)" = \\',
    'f855dfb5f5de0be7cb8ffd6a58d54552aeaada8d',
  ]
  raise "sanitizer execution command drifted" unless lines(relevant.fetch(expected_order[1])) == [
    '"${RUNNER_TEMP}/duckdb-api-v0.1.0/scripts/run-linux-sanitized-cell.sh" \\',
    '"${RUNNER_TEMP}/duckdb-api-v0.1.0/.build/linux-amd64-sanitized" \\',
    '2>&1 | tee "${RUNNER_TEMP}/linux-amd64-sanitized.log"',
  ]
  raise "visible staging command drifted" unless lines(relevant.fetch(expected_order[2])) == [
    'python3 -I scripts/stage-ci-evidence.py \\',
    '"${RUNNER_TEMP}/duckdb-api-v0.1.0" \\',
    '"${RUNNER_TEMP}/duckdb-api-v0.1.0/.build/linux-amd64-sanitized" \\',
    '"${RUNNER_TEMP}/linux-amd64-sanitized.log" \\',
    '"${RUNNER_TEMP}/duckdb-api-sanitizer-custody"',
  ]

  upload = relevant.fetch(expected_order[3])
  raise "upload action pin drifted" unless upload["uses"] == \
    "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02"
  raise "upload inputs drifted" unless upload["with"] == {
    "name" => "linux-amd64-sanitized-${{ github.sha }}",
    "path" => "${{ runner.temp }}/duckdb-api-sanitizer-custody",
    "if-no-files-found" => "error",
    "retention-days" => 90,
  }

  download = relevant.fetch(expected_order[4])
  raise "download action pin drifted" unless download["uses"] == \
    "actions/download-artifact@d3f86a106a0bac45b974a628896c90dbdf5c8093"
  raise "download inputs drifted" unless download["with"] == {
    "name" => "linux-amd64-sanitized-${{ github.sha }}",
    "path" => "${{ runner.temp }}/downloaded-sanitizer-custody",
  }
  raise "download verification command drifted" unless lines(relevant.fetch(expected_order[5])) == [
    'python3 -I scripts/verify-ci-evidence-roundtrip.py \\',
    '"${RUNNER_TEMP}/duckdb-api-v0.1.0" \\',
    '"${RUNNER_TEMP}/duckdb-api-sanitizer-custody" \\',
    '"${RUNNER_TEMP}/downloaded-sanitizer-custody"',
  ]
end

workflow = YAML.load_file(ARGV.fetch(0))
validate(workflow)
mutated = Marshal.load(Marshal.dump(workflow))
mutated.fetch("jobs").fetch("sanitizer").fetch("steps").find do |step|
  step["name"] == "Verify the downloaded evidence bytes"
end["if"] = "${{ false }}"
begin
  validate(mutated)
  raise "structural guard accepted a disabled download verification step"
rescue RuntimeError => error
  raise if error.message == "structural guard accepted a disabled download verification step"
end
puts "sanitizer custody structural workflow guard passed"
RUBY

readonly TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-custody-guard.XXXXXX")"
trap 'rm -rf "${TEST_ROOT}"' EXIT
mkdir "${TEST_ROOT}/source"
for name in compile_commands.json duckdb_api.duckdb_extension envelope.json \
    envelope.sha256 manifest.json manifest.sha256 sanitizer-flags.txt; do
    printf 'fixture\n' >"${TEST_ROOT}/source/${name}"
done
printf 'must be rejected\n' >"${TEST_ROOT}/source/.hidden"
printf 'log\n' >"${TEST_ROOT}/sanitizer.log"

if python3 -I "${REPOSITORY_ROOT}/scripts/stage-ci-evidence.py" \
    "${REPOSITORY_ROOT}" "${TEST_ROOT}/source" "${TEST_ROOT}/sanitizer.log" \
    "${TEST_ROOT}/output" >"${TEST_ROOT}/stdout" 2>"${TEST_ROOT}/stderr"; then
    echo "custody staging accepted a hidden extra input" >&2
    exit 1
fi
if ! rg -F "sanitizer source contains a hidden file" "${TEST_ROOT}/stderr" >/dev/null; then
    echo "custody staging failed for an unexpected reason" >&2
    sed -n '1,120p' "${TEST_ROOT}/stderr" >&2
    exit 1
fi

echo "custody staging hidden-file rejection passed"

rm "${TEST_ROOT}/source/.hidden"
ln -s "${TEST_ROOT}/sanitizer.log" "${TEST_ROOT}/linked-log"
if python3 -I "${REPOSITORY_ROOT}/scripts/stage-ci-evidence.py" \
    "${REPOSITORY_ROOT}" "${TEST_ROOT}/source" "${TEST_ROOT}/linked-log" \
    "${TEST_ROOT}/leaf-output" >"${TEST_ROOT}/leaf-stdout" 2>"${TEST_ROOT}/leaf-stderr"; then
    echo "custody staging accepted a symlinked log leaf" >&2
    exit 1
fi
rg -F "symlink leaf" "${TEST_ROOT}/leaf-stderr" >/dev/null

mkdir "${TEST_ROOT}/canonical-output-parent"
ln -s "${TEST_ROOT}/canonical-output-parent" "${TEST_ROOT}/linked-output-parent"
if python3 -I "${REPOSITORY_ROOT}/scripts/stage-ci-evidence.py" \
    "${REPOSITORY_ROOT}" "${TEST_ROOT}/source" "${TEST_ROOT}/sanitizer.log" \
    "${TEST_ROOT}/linked-output-parent/output" \
    >"${TEST_ROOT}/parent-stdout" 2>"${TEST_ROOT}/parent-stderr"; then
    echo "custody staging accepted a symlinked output parent" >&2
    exit 1
fi
rg -F "output parent must not be a symlink leaf" "${TEST_ROOT}/parent-stderr" >/dev/null
echo "custody staging symlink-boundary rejection passed"

python3 -I - "${REPOSITORY_ROOT}/scripts/verify-ci-evidence-roundtrip.py" \
    "${TEST_ROOT}" <<'PY'
import importlib.util
import pathlib
import shutil
import sys

spec = importlib.util.spec_from_file_location("roundtrip", sys.argv[1])
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)
root = pathlib.Path(sys.argv[2])
staged = root / "synthetic-staged"
downloaded = root / "synthetic-downloaded"
for name in module.ALL_FILES:
    path = staged / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"{name}\n", encoding="utf-8")
shutil.copytree(staged, downloaded)
module.compare_roots(staged, downloaded)
(downloaded / "linux-amd64-sanitized.log").write_text("changed after upload\n")
try:
    module.compare_roots(staged, downloaded)
except AssertionError as error:
    assert "bytes differ from staged input" in str(error)
else:
    raise AssertionError("roundtrip comparison accepted regenerated downloaded custody")

anchor_root = root / "anchor-cases"
anchor_root.mkdir()
manifest = anchor_root / "manifest.json"
manifest.write_text("{}\n", encoding="utf-8")
digest = module.sha256(manifest)
anchor = anchor_root / "manifest.sha256"
anchor.write_text(f"{digest}  manifest.json\n", encoding="utf-8")
module.verify_sha256_anchor(anchor, manifest, "manifest.json", "sanitizer manifest")
for malformed in (
    f"{digest}  other.json\n",
    f"{digest}  manifest.json\ntrailing\n",
):
    anchor.write_text(malformed, encoding="utf-8")
    try:
        module.verify_sha256_anchor(anchor, manifest, "manifest.json", "sanitizer manifest")
    except AssertionError:
        pass
    else:
        raise AssertionError("sanitizer manifest anchor accepted malformed content")
print("staged/downloaded byte comparison counterexample passed")
print("sanitizer manifest anchor counterexamples passed")
PY

echo "local guard proves staging and workflow wiring; an external Actions run is required to prove upload/download transport"
