#!/usr/bin/env python3

from __future__ import annotations

import errno
import hashlib
import json
import os
import pathlib
import re
import stat


ROOT = pathlib.Path(__file__).resolve().parents[1]
HISTORICAL_RELEASES = {
    "0.1.0": {
        "identities": {
            "compiled_connector_sha256": (
                "a3f59ec862769f2e32b234500e9d95e002fb82d64fb0e4881560fb0980ce6072"
            ),
            "fixture_sha256": (
                "1b789738607ae9a74d24b07cacc09e98531c0de5ae9a822e78cabe9c8fc0eb39"
            ),
            "public_contract_sha256": (
                "a18df636619ffd09eae963cc5c6e7d3aa0670e69644380ab6a7c0fb340de2fb2"
            ),
        },
        "pins_canonical_json_sha256": (
            "390437c7e1b37a9b84d7d2e6d0fa46dea079cc000cd8182e9541ecc7a8ee63c6"
        ),
        "public_contract_sha256": (
            "a18df636619ffd09eae963cc5c6e7d3aa0670e69644380ab6a7c0fb340de2fb2"
        ),
    },
    "0.2.0": {
        "identities": {
            "compiled_connector_sha256": (
                "a3f59ec862769f2e32b234500e9d95e002fb82d64fb0e4881560fb0980ce6072"
            ),
            "fixture_sha256": (
                "1b789738607ae9a74d24b07cacc09e98531c0de5ae9a822e78cabe9c8fc0eb39"
            ),
            "public_contract_sha256": (
                "bbba900cb94f6289c9282750ed6d15a5356f6c0de9aa00fa5ae3a0ed8e452160"
            ),
        },
        "pins_canonical_json_sha256": (
            "bd299c28613563794adb4c029fd3fe83641c2fe674c1442f3342b78ca0df1fbd"
        ),
        "public_contract_sha256": (
            "bbba900cb94f6289c9282750ed6d15a5356f6c0de9aa00fa5ae3a0ed8e452160"
        ),
    },
    "0.3.0": {
        "identities": {
            "native_connector_source": {
                "path": "src/connector.cpp",
                "sha256": "d9cf66acedb97b0325ca9c9883afceaa91a491fe48e2f6d5d3744137f8d13e86",
            },
            "public_contract": {
                "canonical_json_sha256": "f5d9a5c14ef603fef34bf7154ad2272e86742fec0af994aacfbfec4afe84c8e9",
                "path": "release/0.3.0/public_contract.json",
            },
        },
        "pins_canonical_json_sha256": (
            "8d19569b10759139708e08e98b84079f0c0c1cde8c1a5380ffd17d57a78ab4a0"
        ),
        "public_contract_sha256": (
            "f5d9a5c14ef603fef34bf7154ad2272e86742fec0af994aacfbfec4afe84c8e9"
        ),
    },
    "0.4.0": {
        "identities_canonical_json_sha256": (
            "69661f15cbb91c5ff57040c6c4c895fdf60f2467ce35081e95f0fda4ed08589c"
        ),
        "pins_canonical_json_sha256": (
            "ad13deb86638aa2e10f760d6ee724ba99a625d0a12c46b24cea3fc57db66d510"
        ),
        "public_contract_sha256": (
            "02e6eb66801e665ed6be5db70706505842ee726090ecc39025d592cad95023b5"
        ),
    },
    "0.5.0": {
        "identities_canonical_json_sha256": (
            "ac6a5534afd0e239b94069fa8df20e4a4f935eba9c5a7e324f1bca606c173ba7"
        ),
        "pins_canonical_json_sha256": (
            "44d0c4c1c2ebc8ff771425471e8681032c65173f650a7767902c2970f4008155"
        ),
        "public_contract_sha256": (
            "c0d7b2ff6160874390a4f06d1b9954ba74604769efcfcf70420fa2702c26fae6"
        ),
    },
    "0.6.0": {
        "identities_canonical_json_sha256": (
            "d584a8df92ef30c3870702de88564c562d575f8f72672249caa30c0f50c5da86"
        ),
        "pins_canonical_json_sha256": (
            "fbb2d0521ae6cf420b29bc9957b9bd7c25d169d5b0a02997f44a4120bd48f012"
        ),
        "public_contract_sha256": (
            "580a33b94357b2a539489ff2c2ba69289b89be3ee2ed019efe29832230f1cecf"
        ),
    },
    "0.7.0": {
        "identities_canonical_json_sha256": (
            "df4ce5ba4e2e6c173ba7a5b7bad0d223b4d6269f0a30cafbaaacd1d8a8902cce"
        ),
        "pins_canonical_json_sha256": (
            "a7a79ad4540a9cb4f5f86b4ab977fe416dec84e1c1e59a0648ddcb01f1737cc6"
        ),
        "public_contract_sha256": (
            "4f21ab8341ccda5c6880addd0febb35eeaa9d14b18d86ac5b3a9a5b9a49385ee"
        ),
    },
}
GIT_ID = re.compile(r"[0-9a-f]{40}")
SHA256 = re.compile(r"[0-9a-f]{64}")
VERSION_PATTERN = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
EXTENSION_CONFIG = re.compile(
    r"[ \t\r\n]*duckdb_extension_load\([ \t\r\n]*"
    r"duckdb_api[ \t\r\n]+"
    r"SOURCE_DIR[ \t\r\n]+\$\{CMAKE_CURRENT_LIST_DIR\}[ \t\r\n]+"
    rf'EXTENSION_VERSION[ \t\r\n]+"(?P<version>{VERSION_PATTERN})"[ \t\r\n]*'
    r"\)[ \t\r\n]*"
)
MAX_IDENTITY_FILE_BYTES = 16 * 1024 * 1024
PATH_BOUND_SHA256 = "sha256-length-prefixed-path-and-bytes-v1"
CURRENT_RELEASE = "0.8.0"
REPOSITORY_CONNECTOR_PACKAGE_ROOT = "connectors/github"
CONTROLLED_PRODUCT_SOURCE_PATHS = (
    "test/cpp/connector/support/catalog_test_access.hpp",
    "test/cpp/connector/support/connector_catalog_test_fixtures.cpp",
    "test/cpp/connector/support/connector_catalog_test_fixtures.hpp",
    "test/cpp/query/integration/controlled_extension_entrypoint.cpp",
    "test/cpp/query/integration/support/controlled_product_composition.cpp",
    "test/cpp/query/integration/support/controlled_product_composition.hpp",
    "test/cpp/runtime/support/loopback_curl_runtime.cpp",
    "test/cpp/runtime/support/loopback_curl_runtime.hpp",
)
CONTROLLED_PUBLIC_EXCLUDED_UNITS = {
    "src/query/duckdb/catalog_generation_coordinator.cpp",
    "src/query/duckdb/extension_entrypoint.cpp",
    "src/query/duckdb/generated_relation_adapter.cpp",
    "src/query/duckdb/package_catalog_snapshot.cpp",
    "src/query/duckdb/package_introspection_functions.cpp",
    "src/query/duckdb/package_lifecycle_sentry.cpp",
    "src/query/duckdb/package_management_functions.cpp",
    "src/query/package_generation_composition.cpp",
    "src/query/product_composition.cpp",
}


class RepositoryReader:
    """Reads stable regular files beneath one repository without following links."""

    def __init__(self, root: pathlib.Path):
        self.root = root

    @staticmethod
    def _relative_parts(relative: str) -> tuple[str, ...]:
        path = pathlib.PurePosixPath(relative)
        if path.is_absolute() or not path.parts or any(
            part in {"", ".", ".."} for part in path.parts
        ):
            raise AssertionError("identity path is not repository-relative")
        return path.parts

    def read_bytes(self, relative: str) -> bytes:
        parts = self._relative_parts(relative)
        directory_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        file_flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
        if hasattr(os, "O_CLOEXEC"):
            directory_flags |= os.O_CLOEXEC
            file_flags |= os.O_CLOEXEC

        descriptors = []
        try:
            root_descriptor = os.open(self.root, directory_flags)
            descriptors.append(root_descriptor)
            parent_descriptor = root_descriptor
            for component in parts[:-1]:
                parent_descriptor = os.open(
                    component, directory_flags, dir_fd=parent_descriptor
                )
                descriptors.append(parent_descriptor)
                if not stat.S_ISDIR(os.fstat(parent_descriptor).st_mode):
                    raise AssertionError(
                        "identity path contains a non-directory component"
                    )

            file_descriptor = os.open(
                parts[-1], file_flags, dir_fd=parent_descriptor
            )
            descriptors.append(file_descriptor)
            before = os.fstat(file_descriptor)
            if not stat.S_ISREG(before.st_mode):
                raise AssertionError("identity path does not name a regular file")
            if before.st_nlink != 1:
                raise AssertionError("identity file has multiple hard links")
            if before.st_size > MAX_IDENTITY_FILE_BYTES:
                raise AssertionError("identity file exceeds the verification byte ceiling")

            chunks = []
            size = 0
            while True:
                chunk = os.read(file_descriptor, 64 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > MAX_IDENTITY_FILE_BYTES:
                    raise AssertionError(
                        "identity file exceeds the verification byte ceiling"
                    )
                chunks.append(chunk)

            after = os.fstat(file_descriptor)
            if after.st_nlink != 1:
                raise AssertionError("identity file has multiple hard links")
            stable_fields = (
                "st_dev",
                "st_ino",
                "st_nlink",
                "st_size",
                "st_mtime_ns",
                "st_ctime_ns",
            )
            if any(getattr(before, field) != getattr(after, field) for field in stable_fields):
                raise AssertionError("identity file changed while it was being read")
            if size != before.st_size:
                raise AssertionError("identity file size changed while it was being read")
            return b"".join(chunks)
        except OSError as error:
            if error.errno in {errno.ELOOP, errno.ENOTDIR}:
                raise AssertionError(
                    "identity path contains a symlink or non-directory component"
                ) from error
            raise AssertionError(f"identity file cannot be read: {relative}") from error
        finally:
            for descriptor in reversed(descriptors):
                os.close(descriptor)

    def read_text(self, relative: str) -> str:
        try:
            return self.read_bytes(relative).decode("utf-8")
        except UnicodeDecodeError as error:
            raise AssertionError("identity file is not valid UTF-8") from error

    def read_json(self, relative: str) -> dict:
        def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict:
            result = {}
            for key, value in pairs:
                if key in result:
                    raise AssertionError("identity file contains a duplicate JSON key")
                result[key] = value
            return result

        try:
            value = json.loads(
                self.read_text(relative), object_pairs_hook=reject_duplicate_keys
            )
        except json.JSONDecodeError as error:
            raise AssertionError("identity file is not valid JSON") from error
        if not isinstance(value, dict):
            raise AssertionError("identity JSON root is not an object")
        return value

    def digest(self, relative: str) -> str:
        return hashlib.sha256(self.read_bytes(relative)).hexdigest()

    def list_regular_files(self, relative_root: str) -> tuple[str, ...]:
        """Inventories a source subtree without accepting links or special files."""
        parts = self._relative_parts(relative_root)
        root = self.root.joinpath(*parts)
        try:
            root_status = root.lstat()
        except OSError as error:
            raise AssertionError(f"identity source root cannot be read: {relative_root}") from error
        if stat.S_ISLNK(root_status.st_mode) or not stat.S_ISDIR(root_status.st_mode):
            raise AssertionError("identity source root is not a real directory")

        result = []

        def visit(directory: pathlib.Path) -> None:
            try:
                entries = sorted(os.scandir(directory), key=lambda entry: entry.name)
            except OSError as error:
                raise AssertionError("identity source inventory cannot be read") from error
            for entry in entries:
                relative = pathlib.Path(entry.path).relative_to(self.root).as_posix()
                if entry.is_symlink():
                    raise AssertionError("identity source inventory contains a symlink")
                if entry.is_dir(follow_symlinks=False):
                    visit(pathlib.Path(entry.path))
                elif entry.is_file(follow_symlinks=False):
                    result.append(relative)
                else:
                    raise AssertionError("identity source inventory contains a special file")

        visit(root)
        return tuple(result)


def canonical_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def path_bound_digest(reader: RepositoryReader, paths: tuple[str, ...]) -> str:
    """Binds normalized repository paths and bytes, never an absolute checkout root."""
    result = hashlib.sha256()
    for path in paths:
        encoded_path = path.encode("utf-8")
        content = reader.read_bytes(path)
        result.update(len(encoded_path).to_bytes(8, "big"))
        result.update(encoded_path)
        result.update(len(content).to_bytes(8, "big"))
        result.update(content)
    return result.hexdigest()


def release_record(reader: RepositoryReader, version: str) -> tuple[dict, dict]:
    release_root = f"release/{version}"
    pins = reader.read_json(f"{release_root}/pins.json")
    public_contract = reader.read_json(f"{release_root}/public_contract.json")
    return pins, public_contract


def current_extension_version(reader: RepositoryReader) -> str:
    extension_config = reader.read_text("extension_config.cmake")
    match = EXTENSION_CONFIG.fullmatch(extension_config)
    if match is None:
        raise AssertionError(
            "extension_config.cmake does not match the sole accepted extension declaration"
        )
    return match.group("version")


def validate_project(pins: dict, version: str) -> None:
    expected = {
        "extension": "duckdb_api",
        "tag": f"v{version}",
        "version": version,
    }
    if pins.get("project") != expected:
        raise AssertionError("current project identity does not match its release pins")


def validate_duckdb(pins: dict, public_contract: dict) -> dict:
    try:
        duckdb = pins["dependencies"]["duckdb"]
        commit = duckdb["commit"]
        tree = duckdb["tree"]
        version = duckdb["version"]
        git_describe = duckdb["git_describe"]
    except (KeyError, TypeError) as error:
        raise AssertionError("current DuckDB identity is incomplete") from error
    if (
        set(duckdb) != {"commit", "git_describe", "tree", "version"}
        or not isinstance(commit, str)
        or GIT_ID.fullmatch(commit) is None
        or not isinstance(tree, str)
        or GIT_ID.fullmatch(tree) is None
        or not isinstance(version, str)
        or git_describe != f"v{version}-0-g{commit[:10]}"
    ):
        raise AssertionError("current DuckDB identity is malformed")
    if public_contract.get("duckdb") != [f"v{version}", commit[:10]]:
        raise AssertionError("current public contract names a different DuckDB identity")
    return duckdb


def validate_source_set_identity(record: object, label: str) -> tuple[str, ...]:
    if not isinstance(record, dict) or set(record) != {"algorithm", "paths", "sha256"}:
        raise AssertionError(f"current {label} identity is malformed")
    if record["algorithm"] != PATH_BOUND_SHA256:
        raise AssertionError(f"current {label} identity uses an unknown algorithm")
    paths = record["paths"]
    if (
        not isinstance(paths, list)
        or not paths
        or any(not isinstance(path, str) for path in paths)
        or paths != sorted(set(paths))
    ):
        raise AssertionError(f"current {label} paths are not a sorted unique list")
    for path in paths:
        RepositoryReader._relative_parts(path)
    if not isinstance(record["sha256"], str) or SHA256.fullmatch(record["sha256"]) is None:
        raise AssertionError(f"current {label} digest is not a lowercase SHA-256")
    return tuple(paths)


def validate_translation_units(value: object, label: str) -> tuple[str, ...]:
    if (
        not isinstance(value, list)
        or not value
        or any(not isinstance(path, str) for path in value)
        or len(value) != len(set(value))
    ):
        raise AssertionError(f"current {label} translation units are malformed")
    for path in value:
        RepositoryReader._relative_parts(path)
        if not path.endswith(".cpp"):
            raise AssertionError(f"current {label} contains a non-translation-unit path")
    return tuple(value)


def validate_current_identities(
    pins: dict, reader: RepositoryReader, version: str
) -> dict:
    identities = pins.get("identities")
    if not isinstance(identities, dict) or set(identities) != {
        "build_graph",
        "controlled_product_sources",
        "native_product_sources",
        "public_contract",
        "repository_connector_package",
    }:
        raise AssertionError("current source identity pins are incomplete")

    native_paths = validate_source_set_identity(
        identities["native_product_sources"], "native product source"
    )
    if native_paths != reader.list_regular_files("src"):
        raise AssertionError("current native product source inventory is incomplete")

    controlled_paths = validate_source_set_identity(
        identities["controlled_product_sources"], "controlled product source"
    )
    if controlled_paths != CONTROLLED_PRODUCT_SOURCE_PATHS:
        raise AssertionError("current controlled product source inventory is incomplete")

    connector_package_paths = validate_source_set_identity(
        identities["repository_connector_package"], "repository connector package"
    )
    if connector_package_paths != reader.list_regular_files(
        REPOSITORY_CONNECTOR_PACKAGE_ROOT
    ):
        raise AssertionError(
            "current repository connector package inventory is incomplete"
        )

    build_graph = identities["build_graph"]
    if not isinstance(build_graph, dict) or set(build_graph) != {
        "controlled_translation_units",
        "public_translation_units",
    }:
        raise AssertionError("current build graph identity is malformed")
    public_units = validate_translation_units(
        build_graph["public_translation_units"], "public product"
    )
    controlled_units = validate_translation_units(
        build_graph["controlled_translation_units"], "controlled product"
    )
    native_units = {path for path in native_paths if path.endswith(".cpp")}
    controlled_only_units = {path for path in controlled_paths if path.endswith(".cpp")}
    if set(public_units) != native_units:
        raise AssertionError("current public build graph omits a native translation unit")
    if not CONTROLLED_PUBLIC_EXCLUDED_UNITS < native_units:
        raise AssertionError("current controlled product exclusion inventory drifted")
    expected_controlled = (
        native_units - CONTROLLED_PUBLIC_EXCLUDED_UNITS
    ) | controlled_only_units
    if set(controlled_units) != expected_controlled:
        raise AssertionError("current controlled build graph has the wrong composition")

    public_contract = identities["public_contract"]
    if not isinstance(public_contract, dict) or set(public_contract) != {
        "canonical_json_sha256",
        "path",
    }:
        raise AssertionError("current public contract identity is malformed")
    if public_contract["path"] != f"release/{version}/public_contract.json":
        raise AssertionError("current public contract identity names the wrong source")
    if (
        not isinstance(public_contract["canonical_json_sha256"], str)
        or SHA256.fullmatch(public_contract["canonical_json_sha256"]) is None
    ):
        raise AssertionError("current public contract digest is not a lowercase SHA-256")
    return identities


def validate_historical_releases(reader: RepositoryReader) -> dict[str, dict]:
    contracts = {}
    for version, expected_release in HISTORICAL_RELEASES.items():
        pins, contract = release_record(reader, version)
        if canonical_digest(pins) != expected_release["pins_canonical_json_sha256"]:
            raise AssertionError(f"historical {version} pins record drifted")
        validate_project(pins, version)
        validate_duckdb(pins, contract)
        expected_identities = expected_release.get("identities")
        if expected_identities is not None:
            identities_match = pins.get("identities") == expected_identities
        else:
            identities_match = canonical_digest(pins.get("identities")) == expected_release[
                "identities_canonical_json_sha256"
            ]
        if not identities_match:
            raise AssertionError(f"historical {version} source identities drifted")
        if canonical_digest(contract) != expected_release["public_contract_sha256"]:
            raise AssertionError(f"historical {version} public contract drifted")
        if contract.get("extension") != ["duckdb_api", version]:
            raise AssertionError(f"historical {version} public contract identity drifted")
        contracts[version] = contract

    expected_0_2 = json.loads(json.dumps(contracts["0.1.0"]))
    expected_0_2["extension"] = ["duckdb_api", "0.2.0"]
    if contracts["0.2.0"] != expected_0_2:
        raise AssertionError(
            "historical 0.2.0 public behavior differs from 0.1.0 beyond extension version"
        )
    return contracts


def verify(root: pathlib.Path = ROOT) -> dict[str, str]:
    reader = RepositoryReader(root)
    version = current_extension_version(reader)
    pins, public_contract = release_record(reader, version)
    validate_project(pins, version)
    duckdb = validate_duckdb(pins, public_contract)
    if public_contract.get("extension") != ["duckdb_api", version]:
        raise AssertionError("current public contract names a different extension identity")

    validate_historical_releases(reader)

    if version != CURRENT_RELEASE:
        raise AssertionError(
            f"current source identity verifier supports only {CURRENT_RELEASE}"
        )
    identities = validate_current_identities(pins, reader, version)
    native_identity = identities["native_product_sources"]
    native_digest = path_bound_digest(reader, tuple(native_identity["paths"]))
    if native_digest != native_identity["sha256"]:
        raise AssertionError("native product source digest does not match the current release pin")
    controlled_identity = identities["controlled_product_sources"]
    controlled_digest = path_bound_digest(reader, tuple(controlled_identity["paths"]))
    if controlled_digest != controlled_identity["sha256"]:
        raise AssertionError("controlled product source digest does not match the current release pin")
    connector_package_identity = identities["repository_connector_package"]
    connector_package_digest = path_bound_digest(
        reader, tuple(connector_package_identity["paths"])
    )
    if connector_package_digest != connector_package_identity["sha256"]:
        raise AssertionError(
            "repository connector package digest does not match the current release pin"
        )
    public_identity = identities["public_contract"]
    actual_public_contract = canonical_digest(public_contract)
    if actual_public_contract != public_identity["canonical_json_sha256"]:
        raise AssertionError("current public contract digest does not match the release pin")
    return {
        "duckdb_commit": duckdb["commit"],
        "duckdb_version": duckdb["version"],
        "controlled_product_sources_sha256": controlled_digest,
        "native_product_sources_sha256": native_digest,
        "public_contract_sha256": actual_public_contract,
        "repository_connector_package_sha256": connector_package_digest,
        "version": version,
    }


def main() -> int:
    print(json.dumps(verify(), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
