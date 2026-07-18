#include "duckdb_api/duckdb_secret.hpp"

#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb_api/execution.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>

namespace duckdb {
namespace {

static const char DUCKDB_API_SECRET_TYPE[] = "duckdb_api";
static const char DUCKDB_API_SECRET_PROVIDER[] = "config";
static const char DUCKDB_API_TOKEN_KEY[] = "token";

[[noreturn]] void ThrowCreationError(const char *message) {
	throw InvalidInputException("[duckdb_api][authentication] %s", message);
}

[[noreturn]] void ThrowResolutionError(const char *message) {
	throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::AUTHENTICATION, "secret", message);
}

unique_ptr<BaseSecret> CreateDuckdbApiSecret(ClientContext &, CreateSecretInput &input) {
	if (!StringUtil::CIEquals(input.type, DUCKDB_API_SECRET_TYPE) ||
	    !StringUtil::CIEquals(input.provider, DUCKDB_API_SECRET_PROVIDER)) {
		ThrowCreationError("secret type and provider do not match the installed credential boundary");
	}
	if (input.persist_type != SecretPersistType::TEMPORARY) {
		ThrowCreationError("duckdb_api secrets require explicit CREATE TEMPORARY SECRET");
	}
	if (!input.storage_type.empty() &&
	    !StringUtil::CIEquals(input.storage_type, SecretManager::TEMPORARY_STORAGE_NAME)) {
		ThrowCreationError("duckdb_api secrets support only temporary memory storage");
	}

	const auto token = input.options.find(DUCKDB_API_TOKEN_KEY);
	if (token == input.options.end() || token->second.IsNull() || token->second.type().id() != LogicalTypeId::VARCHAR ||
	    StringValue::Get(token->second).empty()) {
		ThrowCreationError("TOKEN must be a non-empty VARCHAR");
	}

	auto secret =
	    make_uniq<KeyValueSecret>(input.scope, DUCKDB_API_SECRET_TYPE, DUCKDB_API_SECRET_PROVIDER, input.name);
	secret->secret_map[DUCKDB_API_TOKEN_KEY] = token->second;
	secret->redact_keys.insert(DUCKDB_API_TOKEN_KEY);
	return std::move(secret);
}

bool IsPinnedExactNameAmbiguity(const InvalidConfigurationException &error, const std::string &logical_name) {
	const auto expected = Exception::ConstructMessage(
	    "Ambiguity detected for secret name '%s', secret occurs in multiple storage backends.", logical_name);
	const ErrorData data(error);
	return data.Type() == ExceptionType::INVALID_CONFIGURATION && data.RawMessage() == expected;
}

std::string ResolveToken(ClientContext &context, const std::string &logical_name) {
	if (logical_name.empty()) {
		ThrowResolutionError("a non-empty named duckdb_api secret is required");
	}

	unique_ptr<SecretEntry> entry;
	bool ambiguous = false;
	try {
		auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
		auto &manager = SecretManager::Get(context);
		// Force storage initialization outside the ambiguity catch. DuckDB's
		// all-storage exact-name overload uses InvalidConfigurationException for
		// both initialization defects and name collisions.
		(void)manager.GetSecretByName(transaction, logical_name, SecretManager::TEMPORARY_STORAGE_NAME);
		try {
			entry = manager.GetSecretByName(transaction, logical_name);
		} catch (const InvalidConfigurationException &error) {
			// Pinned DuckDB reports an exact-name collision across secret
			// storages with this exception, but storage implementations can use
			// the same class. Match the pinned manager's complete diagnostic before
			// classifying it as a credential-selection failure; a storage defect
			// propagates to the outer host-failure boundary.
			if (!IsPinnedExactNameAmbiguity(error, logical_name)) {
				throw;
			}
			ambiguous = true;
		}
	} catch (const OutOfMemoryException &) {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "secret",
		                                 "named secret resolution exceeded available memory");
	} catch (const InterruptException &) {
		throw duckdb_api::ExecutionCancelled();
	} catch (const Exception &) {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::INTERNAL, "", "named secret resolution failed");
	} catch (const std::bad_alloc &) {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "secret",
		                                 "named secret resolution exceeded available memory");
	} catch (const std::exception &) {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::INTERNAL, "", "named secret resolution failed");
	} catch (...) {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::INTERNAL, "", "named secret resolution failed");
	}

	if (ambiguous) {
		ThrowResolutionError("named secret could not be resolved unambiguously");
	}
	if (!entry || !entry->secret) {
		ThrowResolutionError("named duckdb_api secret was not found");
	}
	if (entry->persist_type != SecretPersistType::TEMPORARY ||
	    !StringUtil::CIEquals(entry->storage_mode, SecretManager::TEMPORARY_STORAGE_NAME)) {
		ThrowResolutionError("named duckdb_api secret is not temporary memory state");
	}
	if (!StringUtil::CIEquals(entry->secret->GetType(), DUCKDB_API_SECRET_TYPE)) {
		ThrowResolutionError("named secret has the wrong type");
	}
	if (!StringUtil::CIEquals(entry->secret->GetProvider(), DUCKDB_API_SECRET_PROVIDER)) {
		ThrowResolutionError("named duckdb_api secret has the wrong provider");
	}

	const auto *key_value = dynamic_cast<const KeyValueSecret *>(entry->secret.get());
	if (!key_value) {
		ThrowResolutionError("named duckdb_api secret is malformed");
	}
	Value token;
	if (!key_value->TryGetValue(DUCKDB_API_TOKEN_KEY, token) || token.IsNull() ||
	    token.type().id() != LogicalTypeId::VARCHAR || StringValue::Get(token).empty()) {
		ThrowResolutionError("named duckdb_api secret has no usable token");
	}
	return StringValue::Get(token);
}

} // namespace

void RegisterDuckdbApiSecrets(ExtensionLoader &loader) {
	SecretType type;
	type.name = DUCKDB_API_SECRET_TYPE;
	type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
	type.default_provider = DUCKDB_API_SECRET_PROVIDER;
	loader.RegisterSecretType(std::move(type));

	CreateSecretFunction provider;
	provider.secret_type = DUCKDB_API_SECRET_TYPE;
	provider.provider = DUCKDB_API_SECRET_PROVIDER;
	provider.function = CreateDuckdbApiSecret;
	provider.named_parameters[DUCKDB_API_TOKEN_KEY] = LogicalType::VARCHAR;
	loader.RegisterFunction(std::move(provider));
}

duckdb_api::ScanAuthorization ResolveDuckdbApiSecret(ClientContext &context, const std::string &logical_name) {
	// ResolveToken owns and destroys DuckDB's cloned SecretEntry before Runtime
	// receives the only Query-created plaintext snapshot.
	try {
		auto token = ResolveToken(context, logical_name);
		return duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token));
	} catch (const std::bad_alloc &) {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "authorization",
		                                 "authorization capability could not be allocated within its memory budget");
	}
}

} // namespace duckdb
