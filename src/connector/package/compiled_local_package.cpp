#include "compiled_local_package_internal.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api {

class CompiledLocalPackage::State {
public:
	State(std::shared_ptr<const CompiledPackageGeneration> generation_p, connector::PackageSourceSnapshot source_p)
	    : generation(std::move(generation_p)), source(std::move(source_p)) {
		if (!generation || !source.IsValid()) {
			throw std::invalid_argument("compiled local package requires one generation and retained source root");
		}
	}

	std::shared_ptr<const CompiledPackageGeneration> generation;
	connector::PackageSourceSnapshot source;
};

CompiledLocalPackage::CompiledLocalPackage() noexcept {
}

CompiledLocalPackage::CompiledLocalPackage(std::shared_ptr<const State> state_p) : state(std::move(state_p)) {
	if (!state) {
		throw std::invalid_argument("compiled local package state cannot be empty");
	}
}

bool CompiledLocalPackage::IsValid() const noexcept {
	return static_cast<bool>(state);
}

const CompiledPackageGeneration &CompiledLocalPackage::Generation() const {
	if (!state) {
		throw std::logic_error("compiled local package is empty");
	}
	return *state->generation;
}

bool CompiledLocalPackage::MatchesGeneration(const CompiledGenerationHandle &generation) const noexcept {
	return state && state->generation->OpaqueHandle().IsSameGeneration(generation);
}

namespace internal {

CompiledLocalPackage CompiledLocalPackageAccess::Create(std::shared_ptr<const CompiledPackageGeneration> generation,
                                                        connector::PackageSourceSnapshot source) {
	std::unique_ptr<CompiledLocalPackage::State> owned(
	    new CompiledLocalPackage::State(std::move(generation), std::move(source)));
	return CompiledLocalPackage(std::shared_ptr<const CompiledLocalPackage::State>(std::move(owned)));
}

const connector::PackageSourceSnapshot &CompiledLocalPackageAccess::Source(const CompiledLocalPackage &package) {
	if (!package.state) {
		throw std::logic_error("compiled local package is empty");
	}
	return package.state->source;
}

} // namespace internal
} // namespace duckdb_api
