#pragma once

#include <string>
#include <vector>

namespace duckdb_api {
namespace connector {

struct SemanticSourceFile {
	std::string path;
	std::string bytes;
};

// Computes RFC 0013's sha256-length-prefixed-path-and-bytes-v1 identity. Paths
// are validated relative POSIX UTF-8 names, sorted bytewise, and framed as
// u64be(path length), path bytes, u64be(content length), raw content. Metadata,
// absolute roots, README, fixtures, and YAML normalization never enter the
// digest. Invalid or duplicate paths throw std::invalid_argument.
std::string ComputePackageDigest(const std::vector<SemanticSourceFile> &semantic_files);

} // namespace connector
} // namespace duckdb_api
