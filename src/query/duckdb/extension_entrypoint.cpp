#define DUCKDB_EXTENSION_MAIN

#include "duckdb_api_extension.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb_api/product_composition.hpp"

#include <utility>

namespace duckdb {
namespace {

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
	case duckdb_api::ErrorStage::REMOTE_PROTOCOL:
		return "remote_protocol";
	}
	return "internal";
}

void LoadProduct(ExtensionLoader &loader) {
	try {
		auto product = duckdb_api::BuildProductComposition();
		RegisterDuckdbApi(loader, std::move(product.connector), std::move(product.executor));
	} catch (const duckdb_api::ExecutionError &error) {
		if (error.Stage() == duckdb_api::ErrorStage::INTERNAL) {
			throw InvalidInputException("[duckdb_api][internal] extension initialization failed");
		}
		throw InvalidInputException("[duckdb_api][%s] extension initialization failed: %s",
		                            InitializationStageName(error.Stage()), error.SafeMessage());
	} catch (const std::exception &) {
		throw InvalidInputException("[duckdb_api][internal] extension initialization failed");
	} catch (...) {
		throw InvalidInputException("[duckdb_api][internal] extension initialization failed");
	}
}

} // namespace

void DuckdbApiExtension::Load(ExtensionLoader &loader) {
	LoadProduct(loader);
}

std::string DuckdbApiExtension::Name() {
	return "duckdb_api";
}

std::string DuckdbApiExtension::Version() const {
#ifdef EXT_VERSION_DUCKDB_API
	return EXT_VERSION_DUCKDB_API;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(duckdb_api, loader) {
	duckdb::LoadProduct(loader);
}
}
