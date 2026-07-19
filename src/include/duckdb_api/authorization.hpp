#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace duckdb_api {

namespace internal {
class FixedGithubUserBearerAuthenticator;
} // namespace internal

// Runtime-owned authority for opening one scan. Callers must choose either the
// anonymous alternative or the fixed GitHub authenticated-user bearer
// alternative, then move the value into ScanExecutor::OpenWithAuthorization.
//
// GithubUserBearer takes ownership of one token snapshot. That alternative is
// scoped inside Runtime to bearer authentication for the installed GitHub
// authenticated-user profile; callers cannot select a destination, operation,
// authenticator, or header placement. Only Runtime's fixed authenticator may
// consume the opaque state. Moving invalidates the source, and a moved-from
// value fails closed if submitted again.
//
// Destruction is non-throwing and releases the owned token state. The token is
// never copyable, serializable, comparable, or available through a plaintext
// accessor. Runtime minimizes its lifetime but makes no secure-zeroization or
// hostile-process claim. Distinct values share no mutable authorization state
// and may be moved into concurrent scans independently.
class ScanAuthorization {
public:
	static ScanAuthorization Anonymous();
	// The fixed GitHub bearer profile accepts at most 8 KiB of token bytes.
	// This is half the native 16 KiB header envelope, reserving the remainder
	// for fixed project fields and dependency-generated HTTP framing. Query
	// uses this Runtime-owned limit to reject oversized DuckDB secrets before
	// creating or resolving a capability.
	static uint64_t GithubUserBearerTokenByteLimit() noexcept;
	static ScanAuthorization GithubUserBearer(std::string &&token);

	ScanAuthorization(ScanAuthorization &&other) noexcept;
	ScanAuthorization &operator=(ScanAuthorization &&other) noexcept;
	~ScanAuthorization() noexcept = default;

	ScanAuthorization(const ScanAuthorization &) = delete;
	ScanAuthorization &operator=(const ScanAuthorization &) = delete;

private:
	enum class Kind : uint8_t { ANONYMOUS, GITHUB_USER_BEARER };
	class State;
	using StateOwner = std::unique_ptr<State, void (*)(State *)>;

	ScanAuthorization(Kind kind, StateOwner state) noexcept;
	static void DestroyState(State *state) noexcept;

	Kind kind;
	StateOwner state;
	bool valid;

	friend class ScanExecutor;
	friend class internal::FixedGithubUserBearerAuthenticator;
};

} // namespace duckdb_api
