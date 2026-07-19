#define DUCKDB_EXTENSION_MAIN

#include "duckdb_api_extension.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "query/integration/support/controlled_product_composition.hpp"

#include <cstdint>
#include <cstdlib>
#include <utility>

namespace duckdb {
namespace {

static const char CONTROLLED_PORT_ENV[] = "DUCKDB_API_CONTROLLED_PORT";

uint16_t RequiredControlledPort() {
	const auto *value = std::getenv(CONTROLLED_PORT_ENV);
	if (!value || !*value) {
		throw InvalidInputException("[duckdb_api][controlled] private service port is missing");
	}
	uint32_t port = 0;
	for (const auto *cursor = value; *cursor; cursor++) {
		if (*cursor < '0' || *cursor > '9') {
			throw InvalidInputException("[duckdb_api][controlled] private service port is invalid");
		}
		const auto digit = static_cast<uint32_t>(*cursor - '0');
		if (port > (65535U - digit) / 10U) {
			throw InvalidInputException("[duckdb_api][controlled] private service port is invalid");
		}
		port = port * 10U + digit;
	}
	if (port == 0) {
		throw InvalidInputException("[duckdb_api][controlled] private service port is invalid");
	}
	return static_cast<uint16_t>(port);
}

const char *InitializationStageName(duckdb_api::ErrorStage stage) {
	switch (stage) {
	case duckdb_api::ErrorStage::TRANSPORT:
		return "transport";
	case duckdb_api::ErrorStage::HTTP_STATUS:
		return "http_status";
	case duckdb_api::ErrorStage::DECODE:
		return "decode";
	case duckdb_api::ErrorStage::SCHEMA:
		return "schema";
	case duckdb_api::ErrorStage::POLICY:
		return "policy";
	case duckdb_api::ErrorStage::RESOURCE:
		return "resource";
	case duckdb_api::ErrorStage::INTERNAL:
		return "internal";
	case duckdb_api::ErrorStage::AUTHENTICATION:
		return "authentication";
	case duckdb_api::ErrorStage::AUTHORIZATION:
		return "authorization";
	}
	return "internal";
}

void LoadControlledProduct(ExtensionLoader &loader) {
	const auto port = RequiredControlledPort();
	try {
		auto product = duckdb_api_test::BuildControlledProductComposition(port);
		RegisterDuckdbApi(loader, std::move(product.connector), std::move(product.executor));
	} catch (const duckdb_api::ExecutionError &error) {
		if (error.Stage() == duckdb_api::ErrorStage::INTERNAL) {
			throw InvalidInputException("[duckdb_api][internal] controlled extension initialization failed");
		}
		throw InvalidInputException("[duckdb_api][%s] controlled extension initialization failed: %s",
		                            InitializationStageName(error.Stage()), error.SafeMessage());
	} catch (const std::exception &) {
		throw InvalidInputException("[duckdb_api][internal] controlled extension initialization failed");
	} catch (...) {
		throw InvalidInputException("[duckdb_api][internal] controlled extension initialization failed");
	}
}

} // namespace
} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(duckdb_api_controlled, loader) {
	duckdb::LoadControlledProduct(loader);
}
}
