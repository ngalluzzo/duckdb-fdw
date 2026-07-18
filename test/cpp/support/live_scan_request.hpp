#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_request.hpp"

#include <string>

namespace duckdb_api_test {

// Semantics-local request fixtures deliberately use Query's public builder.
// They neither reproduce request construction nor know native relation names,
// schemas, or Connector authentication-policy internals.
inline duckdb_api::ScanRequest BuildAnonymousScanRequest(const duckdb_api::CompiledConnector &connector,
                                                         const std::string &relation_name) {
	return duckdb_api::BuildConservativeScanRequest(connector, relation_name, duckdb_api::LogicalSecretReference());
}

inline duckdb_api::ScanRequest BuildAuthenticatedScanRequest(const duckdb_api::CompiledConnector &connector,
                                                             const std::string &relation_name,
                                                             const std::string &secret_name) {
	return duckdb_api::BuildConservativeScanRequest(connector, relation_name,
	                                                duckdb_api::LogicalSecretReference::Named(secret_name));
}

} // namespace duckdb_api_test
