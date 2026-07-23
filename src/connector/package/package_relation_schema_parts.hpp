#pragma once

#include "package_schema_helpers.hpp"

namespace duckdb_api {
namespace connector {
namespace internal {

OriginDeclaration DecodeHttpOrigin(const SchemaReader &reader);
std::vector<HeaderDeclaration> DecodeHttpHeaders(const SchemaReader &reader);
RestRequestDeclaration DecodeRestRequestSchema(const SchemaReader &reader);
RestResponseDeclaration DecodeRestResponseSchema(const SchemaReader &reader);
RestPaginationDeclaration DecodeRestPaginationSchema(const SchemaReader &reader);
GraphqlRequestDeclaration DecodeGraphqlRequestSchema(const SchemaReader &reader);
OperationDeclaration DecodeOperationSchema(const SchemaReader &reader, bool retry_supported);
PredicateDeclaration DecodePredicateSchema(const SchemaReader &reader);

} // namespace internal
} // namespace connector
} // namespace duckdb_api
