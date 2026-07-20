#include "duckdb_api/planned_protocol_operation.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api {

PlannedProtocolOperation::PlannedProtocolOperation(PlannedProtocol protocol_p,
                                                   std::shared_ptr<const PlannedRestOperation> rest_p,
                                                   std::shared_ptr<const PlannedGraphqlOperation> graphql_p)
    : protocol(protocol_p), rest(std::move(rest_p)), graphql(std::move(graphql_p)) {
}

PlannedProtocolOperation PlannedProtocolOperation::FromRest(PlannedRestOperation operation) {
	return PlannedProtocolOperation(PlannedProtocol::REST,
	                                std::make_shared<const PlannedRestOperation>(std::move(operation)), nullptr);
}

PlannedProtocolOperation PlannedProtocolOperation::FromGraphql(PlannedGraphqlOperation operation) {
	return PlannedProtocolOperation(PlannedProtocol::GRAPHQL, nullptr,
	                                std::make_shared<const PlannedGraphqlOperation>(std::move(operation)));
}

PlannedProtocol PlannedProtocolOperation::Protocol() const {
	return protocol;
}

const PlannedRestOperation &PlannedProtocolOperation::Rest() const {
	if (protocol != PlannedProtocol::REST || !rest || graphql) {
		throw std::logic_error("planned protocol operation does not contain a REST payload");
	}
	return *rest;
}

const PlannedGraphqlOperation &PlannedProtocolOperation::Graphql() const {
	if (protocol != PlannedProtocol::GRAPHQL || !graphql || rest) {
		throw std::logic_error("planned protocol operation does not contain a GraphQL payload");
	}
	return *graphql;
}

} // namespace duckdb_api
