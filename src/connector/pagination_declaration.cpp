#include "duckdb_api/connector_catalog.hpp"
#include "duckdb_api/internal/connector/pagination_declaration.hpp"
#include "duckdb_api/internal/connector/protocol_operation_declaration.hpp"

#include <limits>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

bool FitsBigintPageSequence(std::uint64_t first_page, std::uint64_t max_pages_per_scan) {
	if (first_page == 0 || max_pages_per_scan == 0) {
		return false;
	}
	const auto bigint_max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
	return first_page <= bigint_max && max_pages_per_scan - 1 <= bigint_max - first_page;
}

const char *PaginationStrategyName(CompiledPaginationStrategy strategy) {
	switch (strategy) {
	case CompiledPaginationStrategy::DISABLED:
		return "disabled";
	case CompiledPaginationStrategy::LINK_HEADER:
		return "link_header";
	}
	throw std::logic_error("compiled connector contains an unknown pagination strategy");
}

const char *PageDependencyName(CompiledPageDependency dependency) {
	switch (dependency) {
	case CompiledPageDependency::SEQUENTIAL:
		return "sequential";
	}
	throw std::logic_error("compiled connector contains an unknown pagination dependency");
}

const char *PageConsistencyName(CompiledPageConsistency consistency) {
	switch (consistency) {
	case CompiledPageConsistency::MUTABLE:
		return "mutable";
	}
	throw std::logic_error("compiled connector contains an unknown pagination consistency");
}

const char *LinkRelationName(CompiledLinkRelation relation) {
	switch (relation) {
	case CompiledLinkRelation::NEXT:
		return "next";
	}
	throw std::logic_error("compiled connector contains an unknown Link relation");
}

const char *ContinuationTargetScopeName(CompiledContinuationTargetScope scope) {
	switch (scope) {
	case CompiledContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH:
		return "exact_operation_origin_and_path";
	}
	throw std::logic_error("compiled connector contains an unknown pagination target scope");
}

} // namespace

CompiledPagination CompiledPagination::Disabled() {
	return CompiledPagination();
}

CompiledPagination::CompiledPagination()
    : strategy(CompiledPaginationStrategy::DISABLED), page_size_parameter(), page_size(0), page_number_parameter(),
      first_page(0), page_increment(0), max_pages_per_scan(0) {
}

CompiledPagination::CompiledPagination(std::string page_size_parameter_p, std::uint64_t page_size_p,
                                       std::string page_number_parameter_p, std::uint64_t first_page_p,
                                       std::uint64_t page_increment_p, std::uint64_t max_pages_per_scan_p)
    : strategy(CompiledPaginationStrategy::LINK_HEADER), page_size_parameter(std::move(page_size_parameter_p)),
      page_size(page_size_p), page_number_parameter(std::move(page_number_parameter_p)), first_page(first_page_p),
      page_increment(page_increment_p), max_pages_per_scan(max_pages_per_scan_p) {
	if (!internal::IsCompiledQueryName(page_size_parameter) ||
	    !internal::IsCompiledQueryName(page_number_parameter) ||
	    page_size_parameter == page_number_parameter || page_size == 0 || first_page == 0 || page_increment != 1 ||
	    max_pages_per_scan == 0 || !FitsBigintPageSequence(first_page, max_pages_per_scan)) {
		throw std::invalid_argument("compiled Link pagination contains invalid typed page bindings");
	}
}

void CompiledPagination::RequireLinkHeader() const {
	if (strategy != CompiledPaginationStrategy::LINK_HEADER) {
		throw std::logic_error("disabled pagination has no Link declaration payload");
	}
}

CompiledPaginationStrategy CompiledPagination::Strategy() const {
	return strategy;
}

CompiledPageDependency CompiledPagination::Dependency() const {
	RequireLinkHeader();
	return CompiledPageDependency::SEQUENTIAL;
}

CompiledPageConsistency CompiledPagination::Consistency() const {
	RequireLinkHeader();
	return CompiledPageConsistency::MUTABLE;
}

CompiledLinkRelation CompiledPagination::LinkRelation() const {
	RequireLinkHeader();
	return CompiledLinkRelation::NEXT;
}

CompiledContinuationTargetScope CompiledPagination::TargetScope() const {
	RequireLinkHeader();
	return CompiledContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH;
}

bool CompiledPagination::SupportsTotal() const {
	return false;
}

bool CompiledPagination::SupportsResume() const {
	return false;
}

const std::string &CompiledPagination::PageSizeParameter() const {
	RequireLinkHeader();
	return page_size_parameter;
}

std::uint64_t CompiledPagination::PageSize() const {
	RequireLinkHeader();
	return page_size;
}

const std::string &CompiledPagination::PageNumberParameter() const {
	RequireLinkHeader();
	return page_number_parameter;
}

std::uint64_t CompiledPagination::FirstPage() const {
	RequireLinkHeader();
	return first_page;
}

std::uint64_t CompiledPagination::PageIncrement() const {
	RequireLinkHeader();
	return page_increment;
}

std::uint64_t CompiledPagination::MaxPagesPerScan() const {
	RequireLinkHeader();
	return max_pages_per_scan;
}

namespace internal {

void ValidatePagination(const CompiledOperation &operation) {
	if (operation.Protocol() != CompiledProtocol::REST) {
		throw std::invalid_argument("REST pagination validator received another protocol alternative");
	}
	const auto &rest = operation.Rest();
	const auto &pagination = rest.pagination;
	if (pagination.Strategy() == CompiledPaginationStrategy::DISABLED) {
		for (const auto &parameter : rest.request.query_parameters) {
			if (parameter.source == CompiledQueryValueSource::PAGE_SIZE ||
			    parameter.source == CompiledQueryValueSource::PAGE_NUMBER) {
				throw std::invalid_argument("disabled pagination contains a structural page query field");
			}
		}
		return;
	}
	if (pagination.Strategy() != CompiledPaginationStrategy::LINK_HEADER ||
	    pagination.Dependency() != CompiledPageDependency::SEQUENTIAL ||
	    pagination.Consistency() != CompiledPageConsistency::MUTABLE ||
	    pagination.LinkRelation() != CompiledLinkRelation::NEXT ||
	    pagination.TargetScope() != CompiledContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH ||
	    pagination.SupportsTotal() || pagination.SupportsResume()) {
		throw std::invalid_argument("compiled pagination contains an unsupported capability profile");
	}
	if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY ||
	    (rest.response_source != CompiledResponseSource::JSON_PATH_MANY &&
	     rest.response_source != CompiledResponseSource::ROOT_ARRAY)) {
		throw std::invalid_argument("compiled Link pagination requires a many-row response source");
	}
	if (!IsCompiledQueryName(pagination.PageSizeParameter()) ||
	    !IsCompiledQueryName(pagination.PageNumberParameter()) ||
	    pagination.PageSizeParameter() == pagination.PageNumberParameter() || pagination.PageSize() == 0 ||
	    pagination.FirstPage() == 0 || pagination.PageIncrement() != 1 || pagination.MaxPagesPerScan() == 0 ||
	    pagination.PageSize() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
	    !FitsBigintPageSequence(pagination.FirstPage(), pagination.MaxPagesPerScan())) {
		throw std::invalid_argument("compiled Link pagination contains invalid typed page bindings");
	}
	const CompiledQueryParameter *page_size = nullptr;
	const CompiledQueryParameter *page_number = nullptr;
	for (const auto &parameter : rest.request.query_parameters) {
		if (parameter.source == CompiledQueryValueSource::PAGE_SIZE) {
			if (page_size != nullptr) {
				throw std::invalid_argument("compiled Link pagination contains multiple page-size sources");
			}
			page_size = &parameter;
		}
		if (parameter.source == CompiledQueryValueSource::PAGE_NUMBER) {
			if (page_number != nullptr) {
				throw std::invalid_argument("compiled Link pagination contains multiple page-number sources");
			}
			page_number = &parameter;
		}
	}
	if (page_size == nullptr || page_number == nullptr || page_size->name != pagination.PageSizeParameter() ||
	    page_size->DecodedValue().Bigint() != static_cast<std::int64_t>(pagination.PageSize()) ||
	    page_number->name != pagination.PageNumberParameter() ||
	    page_number->DecodedValue().Bigint() != static_cast<std::int64_t>(pagination.FirstPage())) {
		throw std::invalid_argument("compiled Link pagination disagrees with its structural initial request sources");
	}
}

void AppendPagination(std::ostream &result, const CompiledPagination &pagination) {
	result << PaginationStrategyName(pagination.Strategy());
	if (pagination.Strategy() == CompiledPaginationStrategy::DISABLED) {
		return;
	}
	result << "[relation:" << LinkRelationName(pagination.LinkRelation())
	       << ",dependency:" << PageDependencyName(pagination.Dependency())
	       << ",consistency:" << PageConsistencyName(pagination.Consistency())
	       << ",total:" << (pagination.SupportsTotal() ? "supported" : "none")
	       << ",resume:" << (pagination.SupportsResume() ? "supported" : "none")
	       << ",page_size:" << pagination.PageSizeParameter() << '=' << pagination.PageSize()
	       << ",page_number:" << pagination.PageNumberParameter() << '=' << pagination.FirstPage()
	       << ",increment:" << pagination.PageIncrement()
	       << ",target:" << ContinuationTargetScopeName(pagination.TargetScope())
	       << ",max_pages:" << pagination.MaxPagesPerScan() << ']';
}

} // namespace internal
} // namespace duckdb_api
