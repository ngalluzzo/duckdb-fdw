# DuckDB extension distribution paths: primary-source comparison

Evidence reviewed: **2026-07-17**. This note supplies bounded evidence for RFC
0004. The product manager subsequently selected MIT and aligned with Community
Extensions as the ordinary-user channel. RFC 0004 now records the accepted
compatibility, lifecycle, immutability, rollback/history, and support policy.

## Facts common to every path

DuckDB states that loadable extension binaries are tied to both a specific
DuckDB version and a platform, and refuses incompatible metadata. Its repository
layout therefore includes DuckDB version and platform components. Installation
copies an extension and metadata into DuckDB's extension directory; loading
happens again in every new DuckDB process. These facts make a single macOS ARM64
artifact for DuckDB 1.5.4 evidence for exactly that cell, not a portable release.
[Extension distribution](https://duckdb.org/docs/current/extensions/extension_distribution),
[extension overview](https://duckdb.org/docs/stable/extensions/overview).

DuckDB also states that extensions execute with the privileges of the parent
process. The default permits core- and community-signed extensions; accepting an
unsigned extension requires the startup configuration
`allow_unsigned_extensions`, which permits any unsigned extension in that
process. [Securing extensions](https://duckdb.org/docs/current/operations_manual/securing_duckdb/securing_extensions).

## Comparison

| Path | Build, signature, distribution, and support custody | Version/platform and historical behavior | Licensing/upstream coordination | Security consequence |
| --- | --- | --- | --- | --- |
| Source-built local artifact and explicit path | The project or user builds the bytes. The current 0.1.0 trial produces no DuckDB-recognized signature; the builder transfers and supports them directly. DuckDB supports `INSTALL 'path/to/file.duckdb_extension'` and direct `LOAD`, but this is a mechanism, not a publication service. [Advanced installation](https://duckdb.org/docs/current/extensions/advanced_installation_methods) | Every artifact must target one DuckDB version/platform pair. Historical use requires retaining source, toolchains, dependency pins, and each desired cell's bytes. The current trial proves only 1.5.4 `osx_arm64`. | No upstream listing is required. The project license is MIT; redistribution still requires a dependency/license audit and applicable notices. | With the current unsigned bytes, a process-wide unsigned opt-in is required. Artifact hashes can establish project custody but do not become a DuckDB trust root. |
| Downloadable artifact or custom repository | The project builds, hosts, indexes, and supports every artifact. DuckDB accepts HTTP(S), S3, or local repository layouts and searches compressed and uncompressed forms. The official extension template says a self-managed custom repository gives the publisher more control but requires unsigned loading. [Extension distribution](https://duckdb.org/docs/current/extensions/extension_distribution), [extension template](https://github.com/duckdb/extension-template#uploading-to-a-custom-repository) | Repository paths encode DuckDB version and platform. The publisher must decide which historical DuckDB/platform cells remain present and must avoid replacing immutable versioned bytes silently. The template notes that publishers must update with DuckDB and may build multiple DuckDB versions. | No Community repository approval is required, but the project owns hosting cost, retention, update/rollback, availability, support, dependency obligations, and license notices. | Transport security and a project manifest protect delivery integrity, but DuckDB still treats the artifact as unsigned. A custom URL is not a signature authority and does not narrow `allow_unsigned_extensions`. |
| DuckDB Community Extensions | The extension author maintains source; DuckDB Community CI builds the declared source, signs the outputs with the community key, and distributes them from the centralized endpoint. DuckDB describes community extensions as third-party code not maintained by DuckDB, so extension behavior remains the author's support responsibility. [Community overview](https://duckdb.org/community_extensions/), [security model](https://duckdb.org/docs/current/operations_manual/securing_duckdb/securing_extensions) | Community CI targets supported platforms. Its development guide says distribution aims at the latest stable DuckDB release; older releases become frozen, while upcoming-release compatibility is coordinated with `ref`/`ref_next`. [Community development](https://duckdb.org/community_extensions/development) | Publication requires a descriptor PR and buildability with the Community CI toolchain. Community source must be available, and descriptors include license metadata. MIT satisfies the project-license prerequisite; upstream review, dependency review, and ongoing release maintenance remain. [Community development](https://duckdb.org/community_extensions/development), [Community launch description](https://duckdb.org/2024/07/05/community-extensions) | DuckDB verifies the Community CI signature under its default community trust policy. This avoids process-wide unsigned loading, but it does not mean DuckDB reviewed the extension source; DuckDB explicitly describes third-party extension installation as execution of third-party code. |
| Statically linked custom DuckDB host | The project builds and distributes an entire DuckDB executable or client library with the extension linked in. DuckDB documents extension configuration files and says most extensions built this way are directly linked into the resulting executable. The project owns host packaging and support. [Building extensions](https://duckdb.org/docs/current/dev/building/building_extensions) | The extension and DuckDB are one compiled product cell, so loadable-extension mismatch is removed inside that binary. The publisher must still build, test, retain, and update every host OS/architecture/client combination and every supported historical host release. | No Community listing is required. The project is MIT-licensed but must still satisfy DuckDB and dependency redistribution obligations and decide whether replacing users' normal DuckDB client with a custom host is an acceptable product surface. | There is no separately downloaded extension for DuckDB's extension-signature check to authenticate. Trust moves to the executable/package distribution channel, code-signing/notarization where applicable, and the publisher's update mechanism. Compromise affects the whole custom host. |

## Delivery evidence still missing

The trial can measure exact installation behavior and byte custody, but it
does not prove which latest-stable Community CI platform rows build and pass the
complete stock-DuckDB lifecycle oracle. The Community descriptor, managed
build/sign/publish/install evidence, hosted custody round trip, exact release
matrix, and dependency-license audit remain `0.2.0` delivery gates. They do not
reopen the accepted policy, and MIT does not replace the dependency audit.
