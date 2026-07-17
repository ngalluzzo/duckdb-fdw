#include "duckdb_api/example_composition.hpp"

#include "duckdb_api/embedded_example.hpp"
#include "duckdb_api/internal/fixture_runtime.hpp"

#include <memory>
#include <string>
#include <utility>

namespace duckdb_api {
namespace {

class EmbeddedFixtureSource : public FixtureSource {
public:
	const std::string &ContentDigest() const override {
		static const std::string digest(EXAMPLE_FIXTURE_SHA256);
		return digest;
	}

	void Read(FixtureReadBuffer &buffer) override {
		buffer.Append(EXAMPLE_FIXTURE);
	}
};

class EmbeddedFixtureFactory : public FixtureFactory {
public:
	const std::string &ContentDigest() const override {
		static const std::string digest(EXAMPLE_FIXTURE_SHA256);
		return digest;
	}

	std::unique_ptr<FixtureSource> Open() const override {
		return std::unique_ptr<FixtureSource>(new EmbeddedFixtureSource());
	}
};

} // namespace

ExampleComposition BuildEmbeddedExampleComposition() {
	auto factory = std::shared_ptr<FixtureFactory>(new EmbeddedFixtureFactory());
	ExampleComposition result;
	result.connector = BuildCompiledConnector(factory->ContentDigest());
	result.executor = BuildFixtureScanExecutor(std::move(factory));
	return result;
}

} // namespace duckdb_api
