#include "credential_provider_adapter_internal.hpp"

#include "credential_secret_internal.hpp"
#include "credential_storage_internal.hpp"

#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/function/scalar/variant_utils.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb_api/duckdb_secret.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace duckdb {
namespace duckdb_api_query_internal {
namespace {

[[noreturn]] void ThrowCreationError(const char *message) {
	throw InvalidInputException("[duckdb_api][authentication] %s", message);
}

[[noreturn]] void ThrowCreationHeaderBudgetError() {
	throw InvalidInputException(
	    "[duckdb_api][resource] field=header_bytes: credential exceeds the 8192-byte request-field limit");
}

[[noreturn]] void ThrowProviderFailure() {
	throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::AUTHENTICATION, "credential_provider",
	                                 "credential provider resolution failed");
}

void ValidateCreationEnvelope(const CreateSecretInput &input, CredentialSource source) {
	const auto *expected_provider =
	    source == CredentialSource::CONFIG ? DuckdbApiConfigProvider() : DuckdbApiEnvironmentProvider();
	if (!StringUtil::CIEquals(input.type, DuckdbApiSecretType()) ||
	    !StringUtil::CIEquals(input.provider, expected_provider)) {
		ThrowCreationError("secret type and provider do not match the installed credential boundary");
	}
	if (!input.scope.empty()) {
		ThrowCreationError("duckdb_api credentials do not accept SCOPE");
	}
	if (input.persist_type == SecretPersistType::TEMPORARY) {
		if (!input.storage_type.empty() &&
		    !StringUtil::CIEquals(input.storage_type, SecretManager::TEMPORARY_STORAGE_NAME)) {
			ThrowCreationError("temporary duckdb_api credentials require memory storage");
		}
		return;
	}
	if (input.persist_type == SecretPersistType::PERSISTENT &&
	    StringUtil::CIEquals(input.storage_type, DuckdbApiPersistentStorage())) {
		return;
	}
	ThrowCreationError("use explicit TEMPORARY memory storage or PERSISTENT IN duckdb_api storage");
}

std::string RequiredOption(const CreateSecretInput &input, const char *key, const char *message) {
	const auto option = input.options.find(key);
	if (option == input.options.end() || option->second.IsNull() || option->second.type() != LogicalType::VARIANT()) {
		ThrowCreationError(message);
	}
	Vector encoded(option->second);
	RecursiveUnifiedVectorFormat format;
	Vector::RecursiveToUnifiedFormat(encoded, 1, format);
	UnifiedVariantVectorData data(format);
	auto value = VariantUtils::ConvertVariantToValue(data, 0, 0);
	if (value.IsNull() || value.type() != LogicalType::VARCHAR) {
		ThrowCreationError(message);
	}
	return StringValue::Get(value);
}

unique_ptr<BaseSecret> CreateCredentialSecret(ClientContext &context, CreateSecretInput &input,
                                              CredentialSource source) {
	ValidateCreationEnvelope(input, source);
	std::string payload;
	if (source == CredentialSource::CONFIG) {
		payload = RequiredOption(input, DuckdbApiTokenOption(), "TOKEN must be a non-empty visible-ASCII VARCHAR");
		if (payload.size() > duckdb_api::ScanAuthorization::CredentialByteLimit()) {
			ThrowCreationHeaderBudgetError();
		}
		if (!IsSafeCredentialValue(payload)) {
			ThrowCreationError("TOKEN must be a non-empty visible-ASCII VARCHAR");
		}
	} else {
		payload =
		    RequiredOption(input, DuckdbApiVariableOption(), "VARIABLE must be a portable environment identifier");
		if (!IsPortableEnvironmentName(payload)) {
			ThrowCreationError("VARIABLE must be a portable environment identifier");
		}
	}

	auto &database = DatabaseInstance::GetDatabase(context);
	auto state = LookupCredentialStorageState(database);
	auto authority = GenerateCredentialIdentity();
	const auto revision = GenerateCredentialIdentity();
	if (input.persist_type == SecretPersistType::TEMPORARY) {
		// Set before any attempted store. The conservative bit remains true after
		// a failed create or later drop, but can never suppress a supported entry.
		state->MarkMemoryMayContainDuckdbApi();
		if (input.on_conflict == OnCreateConflict::REPLACE_ON_CONFLICT) {
			auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
			auto existing = SecretManager::Get(context).GetSecretByName(transaction, input.name,
			                                                            SecretManager::TEMPORARY_STORAGE_NAME);
			if (existing && existing->secret) {
				const auto *supported = dynamic_cast<const DuckdbApiSecret *>(existing->secret.get());
				if (supported && existing->persist_type == SecretPersistType::TEMPORARY &&
				    StringUtil::CIEquals(existing->storage_mode, SecretManager::TEMPORARY_STORAGE_NAME)) {
					authority = supported->Authority();
				}
			}
		}
	}
	return make_uniq<DuckdbApiSecret>(input.scope, source, input.name, std::move(payload), authority, revision);
}

unique_ptr<BaseSecret> CreateConfigSecret(ClientContext &context, CreateSecretInput &input) {
	return CreateCredentialSecret(context, input, CredentialSource::CONFIG);
}

unique_ptr<BaseSecret> CreateEnvironmentSecret(ClientContext &context, CreateSecretInput &input) {
	return CreateCredentialSecret(context, input, CredentialSource::ENVIRONMENT);
}

bool IsSupportedMemoryEntry(const SecretEntry &entry) {
	if (!entry.secret || entry.persist_type != SecretPersistType::TEMPORARY ||
	    !StringUtil::CIEquals(entry.storage_mode, SecretManager::TEMPORARY_STORAGE_NAME) ||
	    !StringUtil::CIEquals(entry.secret->GetType(), DuckdbApiSecretType())) {
		return false;
	}
	const auto *secret = dynamic_cast<const DuckdbApiSecret *>(entry.secret.get());
	if (!secret) {
		return false;
	}
	return StringUtil::CIEquals(secret->GetProvider(), DuckdbApiConfigProvider()) ||
	       StringUtil::CIEquals(secret->GetProvider(), DuckdbApiEnvironmentProvider());
}

class DuckdbCredentialProvider final : public duckdb_api::CredentialProvider {
public:
	explicit DuckdbCredentialProvider(ClientContext &context_p) : context(context_p) {
	}

	duckdb_api::CredentialSnapshot Resolve(const duckdb_api::PlannedSecretReference &logical_reference,
	                                       duckdb_api::ExecutionControl &control) const override {
		try {
			if (control.IsCancellationRequested() || context.IsInterrupted()) {
				throw duckdb_api::ExecutionCancelled();
			}
			if (!logical_reference.IsPresent() || logical_reference.Name().empty()) {
				ThrowProviderFailure();
			}
			auto &database = DatabaseInstance::GetDatabase(context);
			auto state = LookupCredentialStorageState(database);
			unique_ptr<SecretEntry> memory_entry;
			if (state->MemoryMayContainDuckdbApi()) {
				if (control.IsCancellationRequested() || context.IsInterrupted()) {
					throw duckdb_api::ExecutionCancelled();
				}
				auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
				auto candidate = SecretManager::Get(context).GetSecretByName(transaction, logical_reference.Name(),
				                                                             SecretManager::TEMPORARY_STORAGE_NAME);
				if (candidate && IsSupportedMemoryEntry(*candidate)) {
					memory_entry = std::move(candidate);
				}
				if (control.IsCancellationRequested() || context.IsInterrupted()) {
					throw duckdb_api::ExecutionCancelled();
				}
			}

			auto persistent =
			    state->SelectPersistentCredential(logical_reference.Name(), memory_entry != nullptr, control);
			const DuckdbApiSecret *selected = nullptr;
			if (memory_entry) {
				selected = dynamic_cast<const DuckdbApiSecret *>(memory_entry->secret.get());
			} else if (persistent) {
				selected = persistent.get();
			}
			if (!selected) {
				ThrowProviderFailure();
			}

			auto value = selected->Payload();
			auto revision = selected->Revision();
			if (selected->Source() == CredentialSource::ENVIRONMENT) {
				if (!IsPortableEnvironmentName(value)) {
					ThrowProviderFailure();
				}
				if (control.IsCancellationRequested() || context.IsInterrupted()) {
					throw duckdb_api::ExecutionCancelled();
				}
				const auto *environment_value = std::getenv(value.c_str());
				if (environment_value == nullptr) {
					ThrowProviderFailure();
				}
				const auto length =
				    strnlen(environment_value,
				            static_cast<std::size_t>(duckdb_api::ScanAuthorization::CredentialByteLimit() + 1));
				if (length == 0 || length > duckdb_api::ScanAuthorization::CredentialByteLimit()) {
					ThrowProviderFailure();
				}
				value.assign(environment_value, length);
				revision = GenerateCredentialIdentity();
				if (control.IsCancellationRequested() || context.IsInterrupted()) {
					throw duckdb_api::ExecutionCancelled();
				}
			}
			if (value.size() > duckdb_api::ScanAuthorization::CredentialByteLimit() || !IsSafeCredentialValue(value)) {
				ThrowProviderFailure();
			}
			if (control.IsCancellationRequested() || context.IsInterrupted()) {
				throw duckdb_api::ExecutionCancelled();
			}
			return StaticCredential(std::move(value), selected->Authority(), revision);
		} catch (const duckdb_api::ExecutionCancelled &) {
			throw;
		} catch (...) {
			ThrowProviderFailure();
		}
	}

private:
	ClientContext &context;
};

} // namespace

void RegisterDuckdbApiCredentialProviders(ExtensionLoader &loader) {
	CreateSecretFunction config;
	config.secret_type = DuckdbApiSecretType();
	config.provider = DuckdbApiConfigProvider();
	config.function = CreateConfigSecret;
	// VARIANT preserves the evaluated SQL type through DuckDB's secret binder.
	// RequiredOption unwraps it and accepts only an original VARCHAR, avoiding
	// both implicit host casts and DuckDB's invalid ANY physical type.
	config.named_parameters[DuckdbApiTokenOption()] = LogicalType::VARIANT();
	loader.RegisterFunction(std::move(config));

	CreateSecretFunction environment;
	environment.secret_type = DuckdbApiSecretType();
	environment.provider = DuckdbApiEnvironmentProvider();
	environment.function = CreateEnvironmentSecret;
	environment.named_parameters[DuckdbApiVariableOption()] = LogicalType::VARIANT();
	loader.RegisterFunction(std::move(environment));
}

std::unique_ptr<duckdb_api::CredentialProvider> CreateDuckdbApiCredentialProvider(ClientContext &context) {
	return std::unique_ptr<duckdb_api::CredentialProvider>(new DuckdbCredentialProvider(context));
}

} // namespace duckdb_api_query_internal
} // namespace duckdb

namespace duckdb {

std::unique_ptr<duckdb_api::CredentialProvider> CreateDuckdbApiCredentialProvider(ClientContext &context) {
	return duckdb_api_query_internal::CreateDuckdbApiCredentialProvider(context);
}

} // namespace duckdb
