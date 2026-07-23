"""External anchors for immutable RFC decision-evidence generations.

The digest of each evidence manifest lives outside that manifest so changing
an artifact and re-pinning the manifest cannot make the evidence gate pass.
"""

AUTHORITIES = {
    "0013": {
        "directory": "docs/rfcs/evidence/0013",
        "manifest_contract": "duckdb_api/rfc0013_evidence_v1",
        "manifest_sha256": "49407e412bd0863fd9d14d881e067e3653bd66dc39e901bf8cceb7f76888f128",
        "verifier_record": {
            "path": "scripts/verify-rfc-0013-evidence.rb",
            "digest": "sha256.02fa2c2507b95c79682d42a72a34daa133039a8141ab42692add70bca4271dc4",
        },
        "production_mirrors": {},
    },
    "0022": {
        "directory": "docs/rfcs/evidence/0022",
        "manifest_contract": "duckdb_api/rfc0022_evidence_v1",
        "manifest_sha256": "f55f12c6550a21778d6177a64da82ae26848a3d8654bf1be435157cb7aa56f2b",
        "verifier_record": {
            "path": "scripts/verify-rfc-evidence.py",
            "authority": "scripts/rfc_evidence_authorities.py",
        },
        "production_mirrors": {
            "connector-package-v1.schema.json": "src/connector/package/assets/connector-package-v1.schema.json",
            "fixture-coverage-v1.json": "src/connector/package/assets/fixture-coverage-v1.json",
            "fixture-index-v1.schema.json": "src/connector/package/assets/fixture-index-v1.schema.json",
        },
    },
}

CURRENT_CONNECTOR_LANGUAGE_AUTHORITY = "0022"
