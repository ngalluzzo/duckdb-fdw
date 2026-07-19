#pragma once

#include "duckdb_api/scan_planner.hpp"

#include <cstdint>
#include <stdexcept>

namespace duckdb_api {
namespace scan_planner_internal {

inline const char *UrlSchemeName(CompiledUrlScheme scheme) {
	switch (scheme) {
	case CompiledUrlScheme::HTTP:
		return "http";
	case CompiledUrlScheme::HTTPS:
		return "https";
	}
	throw std::logic_error("compiled connector contains an unknown URL scheme");
}

inline PlannedUrlScheme PlanUrlScheme(CompiledUrlScheme scheme) {
	switch (scheme) {
	case CompiledUrlScheme::HTTP:
		return PlannedUrlScheme::HTTP;
	case CompiledUrlScheme::HTTPS:
		return PlannedUrlScheme::HTTPS;
	}
	throw std::logic_error("compiled connector contains an unknown URL scheme");
}

inline PlannedProtocol PlanProtocol(CompiledProtocol protocol) {
	switch (protocol) {
	case CompiledProtocol::REST:
		return PlannedProtocol::REST;
	}
	throw std::logic_error("compiled relation contains an unsupported protocol");
}

inline PlannedHttpMethod PlanMethod(CompiledHttpMethod method) {
	switch (method) {
	case CompiledHttpMethod::GET:
		return PlannedHttpMethod::GET;
	}
	throw std::logic_error("compiled relation contains an unsupported HTTP method");
}

inline PlannedReplaySafety PlanReplaySafety(CompiledReplaySafety replay_safety) {
	switch (replay_safety) {
	case CompiledReplaySafety::SAFE:
		return PlannedReplaySafety::SAFE;
	}
	throw std::logic_error("compiled relation contains an unsupported replay-safety declaration");
}

inline PlannedCardinality PlanCardinality(CompiledOperationCardinality cardinality) {
	switch (cardinality) {
	case CompiledOperationCardinality::ZERO_TO_MANY:
		return PlannedCardinality::ZERO_TO_MANY;
	case CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS:
		return PlannedCardinality::EXACTLY_ONE_ON_SUCCESS;
	}
	throw std::logic_error("compiled relation contains an unsupported source cardinality");
}

inline PlannedResponseSource PlanResponseSource(CompiledResponseSource source) {
	switch (source) {
	case CompiledResponseSource::JSON_PATH_MANY:
		return PlannedResponseSource::JSON_PATH_MANY;
	case CompiledResponseSource::ROOT_ARRAY:
		return PlannedResponseSource::ROOT_ARRAY;
	case CompiledResponseSource::ROOT_OBJECT:
		return PlannedResponseSource::ROOT_OBJECT;
	}
	throw std::logic_error("compiled relation contains an unsupported response source");
}

inline BaseDomain PlanBaseDomain(CompiledResponseSource source, CompiledPaginationStrategy pagination) {
	if (pagination == CompiledPaginationStrategy::LINK_HEADER) {
		switch (source) {
		case CompiledResponseSource::JSON_PATH_MANY:
			return BaseDomain::PAGINATED_JSON_PATH_RECORDS;
		case CompiledResponseSource::ROOT_ARRAY:
			return BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS;
		case CompiledResponseSource::ROOT_OBJECT:
			break;
		}
		throw std::logic_error("paginated operation does not expose a many-row response source");
	}
	if (pagination != CompiledPaginationStrategy::DISABLED) {
		throw std::logic_error("compiled relation contains an unsupported pagination strategy");
	}
	switch (source) {
	case CompiledResponseSource::JSON_PATH_MANY:
		return BaseDomain::JSON_PATH_RECORDS;
	case CompiledResponseSource::ROOT_ARRAY:
		throw std::logic_error("root-array response requires an explicit supported pagination declaration");
	case CompiledResponseSource::ROOT_OBJECT:
		return BaseDomain::SUCCESSFUL_ROOT_OBJECT;
	}
	throw std::logic_error("compiled relation contains an unsupported response source");
}

inline PlannedPageDependency PlanPageDependency(CompiledPageDependency dependency) {
	switch (dependency) {
	case CompiledPageDependency::SEQUENTIAL:
		return PlannedPageDependency::SEQUENTIAL;
	}
	throw std::logic_error("compiled relation contains an unsupported pagination dependency");
}

inline PlannedPageConsistency PlanPageConsistency(CompiledPageConsistency consistency) {
	switch (consistency) {
	case CompiledPageConsistency::MUTABLE:
		return PlannedPageConsistency::MUTABLE;
	}
	throw std::logic_error("compiled relation contains an unsupported pagination consistency");
}

inline PlannedLinkRelation PlanLinkRelation(CompiledLinkRelation relation) {
	switch (relation) {
	case CompiledLinkRelation::NEXT:
		return PlannedLinkRelation::NEXT;
	}
	throw std::logic_error("compiled relation contains an unsupported Link relation");
}

inline PlannedContinuationTargetScope PlanTargetScope(CompiledContinuationTargetScope scope) {
	switch (scope) {
	case CompiledContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH:
		return PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH;
	}
	throw std::logic_error("compiled relation contains an unsupported pagination target scope");
}

std::uint64_t BoundedProduct(std::uint64_t left, std::uint64_t right, std::uint64_t ceiling, const char *field);

const CompiledRelation &ValidateAndSelectRelation(const CompiledConnector &connector, const ScanRequest &request);

} // namespace scan_planner_internal
} // namespace duckdb_api
