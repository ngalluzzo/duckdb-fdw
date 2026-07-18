#include "duckdb_api/authorization.hpp"
#include "duckdb_api/execution.hpp"

#include <new>
#include <utility>

namespace duckdb_api {

namespace {

bool IsSafeBearerToken(const std::string &token) {
	if (token.empty()) {
		return false;
	}
	for (const unsigned char byte : token) {
		// The fixed bearer request is an HTTP field value. Restricting the
		// capability snapshot to visible ASCII prevents whitespace ambiguity,
		// header injection, and dependency-specific treatment of control or
		// non-ASCII bytes before any header is materialized.
		if (byte < 0x21 || byte > 0x7e) {
			return false;
		}
	}
	return true;
}

} // namespace

class ScanAuthorization::State {
public:
	explicit State(std::string token_p) : token(std::move(token_p)) {
	}

private:
	std::string token;

	friend class internal::FixedGithubUserBearerAuthenticator;
};

ScanAuthorization::ScanAuthorization(Kind kind_p, StateOwner state_p) noexcept
    : kind(kind_p), state(std::move(state_p)), valid(true) {
}

ScanAuthorization::ScanAuthorization(ScanAuthorization &&other) noexcept
    : kind(other.kind), state(std::move(other.state)), valid(other.valid) {
	other.valid = false;
}

ScanAuthorization &ScanAuthorization::operator=(ScanAuthorization &&other) noexcept {
	if (this != &other) {
		kind = other.kind;
		state = std::move(other.state);
		valid = other.valid;
		other.valid = false;
	}
	return *this;
}

void ScanAuthorization::DestroyState(State *state_p) noexcept {
	delete state_p;
}

ScanAuthorization ScanAuthorization::Anonymous() {
	return ScanAuthorization(Kind::ANONYMOUS, StateOwner(nullptr, &ScanAuthorization::DestroyState));
}

ScanAuthorization ScanAuthorization::GithubUserBearer(std::string &&token) {
	if (!IsSafeBearerToken(token)) {
		throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
		                     "bearer authorization requires a non-empty visible-ASCII token");
	}
	try {
		return ScanAuthorization(Kind::GITHUB_USER_BEARER,
		                         StateOwner(new State(std::move(token)), &ScanAuthorization::DestroyState));
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "authorization",
		                     "authorization capability could not be allocated within its memory budget");
	}
}

} // namespace duckdb_api
