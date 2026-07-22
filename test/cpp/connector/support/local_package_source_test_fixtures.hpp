#pragma once

#include <memory>
#include <string>

namespace duckdb_api_test {

// Connector-owned source fixture for cross-team tests of the public package
// diagnostic boundary. Consumers receive only an absolute root whose accepted
// repository package has one named malformed-YAML failure; package bytes and
// mutation details remain private to this provider service.
class MalformedYamlPackageFixture {
public:
	MalformedYamlPackageFixture(const MalformedYamlPackageFixture &) = default;
	MalformedYamlPackageFixture(MalformedYamlPackageFixture &&) = default;
	MalformedYamlPackageFixture &operator=(const MalformedYamlPackageFixture &) = delete;
	MalformedYamlPackageFixture &operator=(MalformedYamlPackageFixture &&) = delete;

	const std::string &Root() const noexcept;

private:
	class State;
	explicit MalformedYamlPackageFixture(std::shared_ptr<const State> state);
	std::shared_ptr<const State> state;

	friend MalformedYamlPackageFixture BuildRepositoryMalformedYamlPackageFixture(const std::string &);
};

MalformedYamlPackageFixture BuildRepositoryMalformedYamlPackageFixture(const std::string &absolute_repository_root);

// RFC 0019: a byte-identical copy of connectors/github except
// authenticated_repositories.yaml declares `strategy: short_page` instead of
// `strategy: link_next` (its declared page_size_parameter/page_size already
// satisfy short_page's required fields). Used to prove short_page reaches
// real DuckDB EXPLAIN output end to end without modifying the real package.
class ShortPagePackageFixture {
public:
	ShortPagePackageFixture(const ShortPagePackageFixture &) = default;
	ShortPagePackageFixture(ShortPagePackageFixture &&) = default;
	ShortPagePackageFixture &operator=(const ShortPagePackageFixture &) = delete;
	ShortPagePackageFixture &operator=(ShortPagePackageFixture &&) = delete;

	const std::string &Root() const noexcept;

private:
	class State;
	explicit ShortPagePackageFixture(std::shared_ptr<const State> state);
	std::shared_ptr<const State> state;

	friend ShortPagePackageFixture BuildRepositoryShortPagePackageFixture(const std::string &);
};

ShortPagePackageFixture BuildRepositoryShortPagePackageFixture(const std::string &absolute_repository_root);

} // namespace duckdb_api_test
