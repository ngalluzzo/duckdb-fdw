#include "duckdb_api/connector_catalog.hpp"
#include "support/connector_catalog_test_access.hpp"
#include "support/connector_pagination_contract.hpp"
#include "support/require.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::ConnectorCatalogTestAccess;
using duckdb_api_test::Require;

#define DEFINE_MEMBER_PROBE(PROBE_NAME, MEMBER_NAME)                                                                   \
	template <typename T>                                                                                              \
	class PROBE_NAME {                                                                                                 \
		template <typename U>                                                                                          \
		static char Test(decltype(&U::MEMBER_NAME));                                                                   \
		template <typename U>                                                                                          \
		static long Test(...);                                                                                         \
                                                                                                                       \
	public:                                                                                                            \
		static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);                                                  \
	}

DEFINE_MEMBER_PROBE(HasPaginationEnabledMember, pagination_enabled);
DEFINE_MEMBER_PROBE(HasResponseUrlMember, response_url);
DEFINE_MEMBER_PROBE(HasLinkValueMember, link_value);

#undef DEFINE_MEMBER_PROBE

static_assert(!HasPaginationEnabledMember<duckdb_api::CompiledOperation>::VALUE,
              "pagination must be an explicit closed declaration rather than a boolean");
static_assert(!HasResponseUrlMember<duckdb_api::CompiledPagination>::VALUE,
              "compiled pagination must not retain a response-granted URL");
static_assert(!HasLinkValueMember<duckdb_api::CompiledPagination>::VALUE,
              "compiled pagination must not retain mutable Link response data");
static_assert(std::is_same<decltype(duckdb_api::CompiledOperation::pagination), duckdb_api::CompiledPagination>::value,
              "CompiledOperation must carry the closed pagination value");
static_assert(std::is_copy_constructible<duckdb_api::CompiledPagination>::value,
              "immutable pagination must support relation/catalog copies");
static_assert(std::is_move_constructible<duckdb_api::CompiledPagination>::value,
              "immutable pagination must support relation/catalog ownership transfer");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledPagination>::value,
              "pagination assignment would permit post-construction replacement");
static_assert(!std::is_move_assignable<duckdb_api::CompiledPagination>::value,
              "pagination assignment would permit post-construction replacement");
static_assert(!std::is_default_constructible<duckdb_api::CompiledPagination>::value,
              "pagination must not admit a payload-free ambiguous default");
static_assert(!std::is_constructible<duckdb_api::CompiledPagination, bool>::value,
              "production consumers must not enable pagination with a boolean");
static_assert(std::is_copy_constructible<duckdb_api::CompiledResourceCeilings>::value,
              "immutable resource declarations must support catalog copies");
static_assert(!std::is_default_constructible<duckdb_api::CompiledResourceCeilings>::value,
              "resource declarations must not admit a partial default");
static_assert(!std::is_constructible<duckdb_api::CompiledResourceCeilings, std::uint64_t, std::uint64_t>::value,
              "production consumers must not construct relation resource authority");

template <typename Callable>
void RequireInvalid(const std::string &message, Callable callback) {
	bool rejected = false;
	try {
		callback();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, message);
}

template <typename Callable>
void RequireLogicError(const std::string &message, Callable callback) {
	bool rejected = false;
	try {
		callback();
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, message);
}

std::vector<duckdb_api::CompiledColumn> Columns() {
	return {{"id", "BIGINT", false, "$.id"}};
}

duckdb_api::CompiledOperation BuildDisabledOperation() {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("api.github.com"), 443};
	return duckdb_api::CompiledOperation {"fixture_rows",
	                                      true,
	                                      duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                      duckdb_api::CompiledProtocol::REST,
	                                      duckdb_api::CompiledHttpMethod::GET,
	                                      duckdb_api::CompiledReplaySafety::SAFE,
	                                      false,
	                                      ConnectorCatalogTestAccess::DisabledPagination(),
	                                      {origin, "/rows", {}, {{"X-Fixture", "safe"}}},
	                                      duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	                                      "$.items[*]"};
}

duckdb_api::CompiledOperation BuildPaginatedOperation(
    duckdb_api::CompiledPagination pagination,
    std::vector<duckdb_api::CompiledQueryParameter> query_parameters = {{"page_size", "5"}, {"page", "1"}}) {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("api.github.com"), 443};
	return duckdb_api::CompiledOperation {
	    "fixture_link_rows",
	    true,
	    duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	    duckdb_api::CompiledProtocol::REST,
	    duckdb_api::CompiledHttpMethod::GET,
	    duckdb_api::CompiledReplaySafety::SAFE,
	    false,
	    std::move(pagination),
	    {origin, "/linked-rows", std::move(query_parameters), {{"X-Fixture", "safe"}}},
	    duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	    "$.items[*]"};
}

duckdb_api::CompiledConnector BuildValidPaginatedCatalog() {
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    "linked_rows", Columns(),
	    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4)),
	    ConnectorCatalogTestAccess::RequiredBearer(),
	    ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 5, 20, 64)));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "pagination_fixture", "1.0.0",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 2048});
}

void TestClosedValuesAndExplanation() {
	const auto unpaginated = ConnectorCatalogTestAccess::Relation(
	    "rows", Columns(), BuildDisabledOperation(), ConnectorCatalogTestAccess::Anonymous(),
	    ConnectorCatalogTestAccess::UnpaginatedResources(2, 64));
	Require(unpaginated.Operation().pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::DISABLED,
	        "unpaginated relation gained a pagination declaration");
	Require(!unpaginated.ResourceCeilings().HasResponseByteNarrowing() &&
	            unpaginated.ResourceCeilings().MaxRecordsPerPage() ==
	                unpaginated.ResourceCeilings().MaxRecordsPerScan(),
	        "unpaginated relation lost its one-page resource scope");
	RequireLogicError("disabled pagination exposed Link payload",
	                  [&unpaginated]() { (void)unpaginated.Operation().pagination.PageSizeParameter(); });
	RequireLogicError("unscoped resources exposed response-byte payload",
	                  [&unpaginated]() { (void)unpaginated.ResourceCeilings().MaxResponseBytesPerPage(); });

	const auto catalog = BuildValidPaginatedCatalog();
	const auto &paginated = catalog.Relations()[0];
	Require(paginated.Operation().pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::LINK_HEADER &&
	            paginated.Operation().pagination.Dependency() == duckdb_api::CompiledPageDependency::SEQUENTIAL &&
	            paginated.Operation().pagination.Consistency() == duckdb_api::CompiledPageConsistency::MUTABLE &&
	            paginated.Operation().pagination.LinkRelation() == duckdb_api::CompiledLinkRelation::NEXT,
	        "paginated relation lost its closed capability declaration");
	Require(paginated.Operation().pagination.TargetScope() ==
	                duckdb_api::CompiledContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH &&
	            !paginated.Operation().pagination.SupportsTotal() && !paginated.Operation().pagination.SupportsResume(),
	        "paginated relation widened continuation or consistency claims");
	Require(paginated.ResourceCeilings().HasResponseByteNarrowing() &&
	            paginated.ResourceCeilings().MaxResponseBytesPerPage() == 1024 &&
	            paginated.ResourceCeilings().MaxResponseBytesPerScan() == 4096 &&
	            paginated.ResourceCeilings().MaxRecordsPerPage() == 5 &&
	            paginated.ResourceCeilings().MaxRecordsPerScan() == 20,
	        "paginated relation lost distinct page and scan resources");
	Require(catalog.Snapshot().find("pagination:link_header[relation:next,dependency:sequential,") != std::string::npos,
	        "safe snapshot omitted explicit pagination semantics");
	Require(catalog.Snapshot().find(
	            "response_bytes_per_page:1024,response_bytes_per_scan:4096,records_per_page:5,records_per_scan:20") !=
	            std::string::npos,
	        "safe snapshot omitted scoped pagination resources");
	for (const auto &prohibited : {"response_url=", "Link=", "secret_name=", "Authorization="}) {
		Require(catalog.Snapshot().find(prohibited) == std::string::npos,
		        "pagination explanation retained execution authority: " + std::string(prohibited));
	}
}

void TestPaginationProfileValidation() {
	RequireInvalid("Link pagination accepted an empty page-size parameter",
	               []() { (void)ConnectorCatalogTestAccess::SequentialLink("", 5, "page", 1, 1, 4); });
	RequireInvalid("Link pagination accepted duplicate page parameter names",
	               []() { (void)ConnectorCatalogTestAccess::SequentialLink("page", 5, "page", 1, 1, 4); });
	RequireInvalid("Link pagination accepted a zero page size",
	               []() { (void)ConnectorCatalogTestAccess::SequentialLink("page_size", 0, "page", 1, 1, 4); });
	RequireInvalid("Link pagination accepted a zero first page",
	               []() { (void)ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 0, 1, 4); });
	RequireInvalid("Link pagination accepted a non-unit page transition",
	               []() { (void)ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 2, 4); });
	RequireInvalid("Link pagination accepted an empty scan page budget",
	               []() { (void)ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 0); });

	RequireInvalid("Link pagination inferred a mismatched fixed page size", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4),
		                            {{"page_size", "4"}, {"page", "1"}});
		ConnectorCatalogTestAccess::Relation("mismatched_page_size", Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 5, 20, 64));
	});
	RequireInvalid("Link pagination inferred reordered fixed bindings", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4),
		                            {{"page", "1"}, {"page_size", "5"}});
		ConnectorCatalogTestAccess::Relation("reordered_page_bindings", Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 5, 20, 64));
	});
	RequireInvalid("root-object operation accepted Link pagination", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4));
		operation.cardinality = duckdb_api::CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS;
		operation.response_source = duckdb_api::CompiledResponseSource::ROOT_OBJECT;
		operation.records_extractor = "$";
		ConnectorCatalogTestAccess::Relation("paginated_root", Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 1, 4, 64));
	});
	{
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4));
		operation.response_source = duckdb_api::CompiledResponseSource::ROOT_ARRAY;
		operation.records_extractor = "$";
		const auto root_array = ConnectorCatalogTestAccess::Relation(
		    "paginated_root_array", Columns(), std::move(operation), ConnectorCatalogTestAccess::RequiredBearer(),
		    ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 5, 20, 64));
		Require(root_array.Operation().response_source == duckdb_api::CompiledResponseSource::ROOT_ARRAY,
		        "typed paginated root-array source was not retained");
	}
}

void TestScopedResourceValidation() {
	RequireInvalid("relation accepted an empty resource ceiling", []() {
		ConnectorCatalogTestAccess::Relation("empty_resources", Columns(), BuildDisabledOperation(),
		                                     ConnectorCatalogTestAccess::Anonymous(),
		                                     ConnectorCatalogTestAccess::UnpaginatedResources(2, 0));
	});
	RequireInvalid("unpaginated relation accepted different page and scan records", []() {
		ConnectorCatalogTestAccess::Relation("widened_unpaginated", Columns(), BuildDisabledOperation(),
		                                     ConnectorCatalogTestAccess::Anonymous(),
		                                     ConnectorCatalogTestAccess::PaginatedResources(1024, 1024, 2, 4, 64));
	});
	RequireInvalid("paginated relation inherited an unscoped response ceiling", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4));
		ConnectorCatalogTestAccess::Relation("missing_response_scope", Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     ConnectorCatalogTestAccess::UnpaginatedResources(5, 64));
	});
	RequireInvalid("paginated relation accepted a page size above its decoder ceiling", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4));
		ConnectorCatalogTestAccess::Relation("undersized_page_records", Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 4, 16, 64));
	});
	RequireInvalid("paginated relation accepted records beyond its page sequence", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4));
		ConnectorCatalogTestAccess::Relation("widened_scan_records", Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 5, 21, 64));
	});
	RequireInvalid("paginated relation accepted response bytes beyond its page sequence", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4));
		ConnectorCatalogTestAccess::Relation("widened_scan_response", Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     ConnectorCatalogTestAccess::PaginatedResources(1024, 4097, 5, 20, 64));
	});
	RequireInvalid("paginated relation accepted inconsistent response byte scopes", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 4));
		ConnectorCatalogTestAccess::Relation("inconsistent_response_scope", Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     ConnectorCatalogTestAccess::PaginatedResources(1024, 512, 5, 20, 64));
	});
	RequireInvalid("paginated relation accepted an overflowing page resource envelope", []() {
		auto operation =
		    BuildPaginatedOperation(ConnectorCatalogTestAccess::SequentialLink("page_size", 5, "page", 1, 1, 2));
		ConnectorCatalogTestAccess::Relation(
		    "overflowing_page_resources", Columns(), std::move(operation), ConnectorCatalogTestAccess::RequiredBearer(),
		    ConnectorCatalogTestAccess::PaginatedResources(std::numeric_limits<std::uint64_t>::max(),
		                                                   std::numeric_limits<std::uint64_t>::max(), 5, 10, 64));
	});
	RequireInvalid("catalog accepted a relation response ceiling wider than connector policy", []() {
		const auto paginated = BuildValidPaginatedCatalog();
		auto policy = paginated.NetworkPolicy();
		policy.max_response_bytes = 512;
		ConnectorCatalogTestAccess::Catalog(paginated.Origin(), paginated.ConnectorName(), paginated.Version(),
		                                    paginated.Relations(), std::move(policy));
	});
}

} // namespace

namespace duckdb_api_test {

void RunConnectorPaginationContractTests() {
	TestClosedValuesAndExplanation();
	TestPaginationProfileValidation();
	TestScopedResourceValidation();
}

} // namespace duckdb_api_test
