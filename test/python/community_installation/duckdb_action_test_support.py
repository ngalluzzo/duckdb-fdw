"""Deterministic DuckDB connection fixture shared by action-oracle tests."""

from __future__ import annotations

import pathlib

try:
    from .test_support import GIT_C
except ImportError:
    from test_support import GIT_C


class FakeConnection:
    """Record Query's DuckDB commands and expose the accepted stock catalogs."""

    def __init__(self, artifact: pathlib.Path, *, incompatible: bool = False):
        self.artifact = artifact
        self.incompatible = incompatible
        self.installed = False
        self.loaded = False
        self.last = ""
        self.closed = False
        self.commands: list[str] = []
        self.description: list[tuple[str, str]] = []

    def execute(self, query: str, parameters=None):
        self.last = " ".join(query.split())
        if "function_name = ?" in self.last:
            if parameters != ["duckdb_api_scan"]:
                raise AssertionError(
                    f"unexpected function inventory parameters: {parameters!r}"
                )
        elif parameters is not None:
            raise AssertionError(f"unexpected bound parameters: {parameters!r}")
        self.commands.append(self.last)
        if self.last.startswith("INSTALL"):
            if self.incompatible:
                raise RuntimeError(
                    "artifact targets v1.5.4; current host version is v1.5.3"
                )
            self.installed = True
        elif self.last == "LOAD duckdb_api":
            if not self.installed:
                raise RuntimeError("duckdb_api is not installed")
            self.loaded = True
        elif self.last.startswith("SELECT id, name, active FROM duckdb_api_scan"):
            self.description = [
                ("id", "BIGINT"),
                ("name", "VARCHAR"),
                ("active", "BOOLEAN"),
            ]
        return self

    def fetchone(self):
        if self.last == "PRAGMA version":
            return ("v1.5.4", GIT_C[:10], "Variegata")
        if self.last == "PRAGMA platform":
            return ("osx_arm64",)
        if "allow_unsigned_extensions" in self.last:
            return ("false",)
        if "FROM duckdb_extensions()" in self.last:
            if not self.installed:
                return None
            return (
                "duckdb_api",
                "0.2.0",
                self.loaded,
                True,
                "REPOSITORY",
                "community",
                str(self.artifact),
            )
        if "count(*) FROM duckdb_functions()" in self.last:
            return (1 if self.loaded else 0,)
        raise AssertionError(f"unexpected fetchone query: {self.last}")

    def fetchall(self):
        if self.last == "SELECT name FROM duckdb_settings()":
            return [("allow_unsigned_extensions",)]
        if "FROM duckdb_types()" in self.last:
            return []
        if self.last.startswith("SELECT id, name, active FROM duckdb_api_scan"):
            return [(1, "alpha", True), (2, "beta", False), (3, "gamma", True)]
        if "SELECT parameters, parameter_types" in self.last:
            return [(["connector", "relation"], ["VARCHAR", "VARCHAR"])]
        raise AssertionError(f"unexpected fetchall query: {self.last}")

    def close(self):
        self.closed = True
