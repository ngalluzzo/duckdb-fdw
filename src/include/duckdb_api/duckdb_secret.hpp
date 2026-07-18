#pragma once

#include "duckdb_api/authorization.hpp"

#include <string>

namespace duckdb {

class ClientContext;
class ExtensionLoader;

// Register Query Experience's DuckDB-specific secret boundary. The type is
// `duckdb_api`; its sole provider is `config`, and that provider accepts only a
// redacted TOKEN VARCHAR for explicitly temporary memory storage. It performs
// no environment, filesystem, network, or implicit credential lookup.
//
// DuckDB 1.5.4 exposes separate, non-transactional registration calls for a
// secret type and provider and no corresponding unregister operation. A later
// provider or table-function failure can therefore leave an orphan type or
// provider in this DatabaseInstance. Callers must invoke this function before
// publishing duckdb_api_scan and fail the load rather than attempting repair.
void RegisterDuckdbApiSecrets(ExtensionLoader &loader);

// Resolve the current case-insensitive exact name through DuckDB's system-
// catalog transaction and return one opaque Runtime capability. Resolution
// accepts only a `duckdb_api/config` KeyValueSecret whose host entry is
// explicitly temporary and stored in `memory`; missing, ambiguous, malformed,
// or differently typed or stored entries fail closed without exposing values.
//
// The ClientContext, CatalogTransaction, SecretEntry, and plaintext token are
// call-scoped. The DuckDB objects are destroyed before this function transfers
// token ownership to ScanAuthorization, and none is retained by Query. Each
// call therefore observes replacement or drop independently. Runtime, not this
// module, owns bearer placement, destination policy, capability teardown, and
// the explicit absence of a secure-zeroization guarantee.
duckdb_api::ScanAuthorization ResolveDuckdbApiSecret(ClientContext &context, const std::string &logical_name);

} // namespace duckdb
