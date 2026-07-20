#include "duckdb_api/internal/connector/operation_selector_declaration.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

bool IsSelectorInputIdentifier(const std::string &value) {
	if (value.empty() || value.front() < 'a' || value.front() > 'z') {
		return false;
	}
	for (const auto character : value) {
		const bool lower = character >= 'a' && character <= 'z';
		const bool digit = character >= '0' && character <= '9';
		if (!lower && !digit && character != '_') {
			return false;
		}
	}
	return true;
}

void NormalizeSelectorInputSet(std::vector<std::string> &inputs, bool require_nonempty) {
	if (require_nonempty && inputs.empty()) {
		throw std::invalid_argument("compiled operation selector contains an empty any-input alternative");
	}
	for (const auto &input : inputs) {
		if (!IsSelectorInputIdentifier(input)) {
			throw std::invalid_argument("compiled operation selector contains an invalid input identifier");
		}
	}
	std::sort(inputs.begin(), inputs.end());
	if (std::adjacent_find(inputs.begin(), inputs.end()) != inputs.end()) {
		throw std::invalid_argument("compiled operation selector contains a duplicate input identifier");
	}
}

bool SelectorSetsOverlap(const std::vector<std::string> &left, const std::vector<std::string> &right) {
	for (const auto &input : left) {
		if (std::find(right.begin(), right.end(), input) != right.end()) {
			return true;
		}
	}
	return false;
}

bool SelectorInputIsRepresentable(const CompiledOperation &operation,
                                  const std::vector<CompiledRelationInput> &relation_inputs,
                                  const std::vector<CompiledPredicateMapping> &mappings, const std::string &input) {
	for (const auto &relation_input : relation_inputs) {
		if (relation_input.Name() == input) {
			return true;
		}
	}
	for (const auto &mapping : mappings) {
		if (mapping.OperationName() == operation.name && mapping.RemoteInputName() == input) {
			return true;
		}
	}
	return false;
}

} // namespace

CompiledOperationSelector::CompiledOperationSelector() : priority(0) {
}

CompiledOperationSelector::CompiledOperationSelector(std::vector<std::string> required_inputs_p,
                                                     std::vector<std::vector<std::string>> any_input_sets_p,
                                                     std::vector<std::string> forbidden_inputs_p,
                                                     std::int32_t priority_p)
    : required_inputs(std::move(required_inputs_p)), any_input_sets(std::move(any_input_sets_p)),
      forbidden_inputs(std::move(forbidden_inputs_p)), priority(priority_p) {
	NormalizeSelectorInputSet(required_inputs, false);
	NormalizeSelectorInputSet(forbidden_inputs, false);
	for (auto &alternative : any_input_sets) {
		NormalizeSelectorInputSet(alternative, true);
	}
	std::sort(any_input_sets.begin(), any_input_sets.end());
	if (std::adjacent_find(any_input_sets.begin(), any_input_sets.end()) != any_input_sets.end()) {
		throw std::invalid_argument("compiled operation selector contains a duplicate any-input alternative");
	}
	if (SelectorSetsOverlap(required_inputs, forbidden_inputs)) {
		throw std::invalid_argument("compiled operation selector both requires and forbids an input");
	}
	for (const auto &alternative : any_input_sets) {
		if (SelectorSetsOverlap(alternative, forbidden_inputs)) {
			throw std::invalid_argument("compiled operation selector any-input alternative contains a forbidden input");
		}
	}
}

const std::vector<std::string> &CompiledOperationSelector::RequiredInputs() const {
	return required_inputs;
}

const std::vector<std::vector<std::string>> &CompiledOperationSelector::AnyInputSets() const {
	return any_input_sets;
}

const std::vector<std::string> &CompiledOperationSelector::ForbiddenInputs() const {
	return forbidden_inputs;
}

std::int32_t CompiledOperationSelector::Priority() const {
	return priority;
}

namespace internal {

void ValidateOperationSelectorReferences(const CompiledOperation &operation,
                                         const std::vector<CompiledRelationInput> &relation_inputs,
                                         const std::vector<CompiledPredicateMapping> &mappings) {
	const auto validate = [&operation, &relation_inputs, &mappings](const std::vector<std::string> &inputs) {
		for (const auto &input : inputs) {
			if (!SelectorInputIsRepresentable(operation, relation_inputs, mappings, input)) {
				throw std::invalid_argument("compiled operation selector references an unrepresentable input");
			}
		}
	};
	validate(operation.selector.RequiredInputs());
	for (const auto &alternative : operation.selector.AnyInputSets()) {
		validate(alternative);
	}
	validate(operation.selector.ForbiddenInputs());
}

} // namespace internal
} // namespace duckdb_api
