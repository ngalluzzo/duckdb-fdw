#include "duckdb_api/internal/connector/operation_selector_declaration.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

bool IsSelectorInputIdentifier(const std::string &value) {
	if (value.empty() || value.size() > 63 || value.front() < 'a' || value.front() > 'z') {
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

void ValidateReferenceKind(CompiledRequiredInputKind kind) {
	switch (kind) {
	case CompiledRequiredInputKind::RELATION_INPUT:
	case CompiledRequiredInputKind::CONDITIONAL_INPUT:
		return;
	}
	throw std::invalid_argument("compiled required-input reference contains an unknown namespace");
}

bool ReferenceLess(const CompiledRequiredInputReference &left, const CompiledRequiredInputReference &right) {
	if (left.Kind() != right.Kind()) {
		return static_cast<int>(left.Kind()) < static_cast<int>(right.Kind());
	}
	return left.Id() < right.Id();
}

std::vector<CompiledRequiredInputReference>
NormalizeRequiredReferences(const std::vector<CompiledRequiredInputReference> &references) {
	std::vector<CompiledRequiredInputReference> normalized;
	normalized.reserve(references.size());
	std::vector<bool> emitted(references.size(), false);
	for (std::size_t output = 0; output < references.size(); output++) {
		std::size_t selected = references.size();
		for (std::size_t index = 0; index < references.size(); index++) {
			if (!emitted[index] &&
			    (selected == references.size() || ReferenceLess(references[index], references[selected]))) {
				selected = index;
			}
		}
		if (!normalized.empty() && normalized.back().Kind() == references[selected].Kind() &&
		    normalized.back().Id() == references[selected].Id()) {
			throw std::invalid_argument("compiled operation selector contains a duplicate tagged required input");
		}
		normalized.push_back(references[selected]);
		emitted[selected] = true;
	}
	return normalized;
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

bool HasRelationInput(const std::vector<CompiledRelationInput> &relation_inputs, const std::string &input) {
	for (const auto &relation_input : relation_inputs) {
		if (relation_input.Name() == input) {
			return true;
		}
	}
	return false;
}

bool HasConditionalInput(const CompiledOperation &operation, const std::vector<CompiledPredicateMapping> &mappings,
                         const std::string &input) {
	for (const auto &mapping : mappings) {
		if (mapping.OperationName() == operation.name && mapping.RemoteInputName() == input) {
			return true;
		}
	}
	return false;
}

bool LegacySelectorInputIsRepresentable(const CompiledOperation &operation,
                                        const std::vector<CompiledRelationInput> &relation_inputs,
                                        const std::vector<CompiledPredicateMapping> &mappings,
                                        const std::string &input) {
	return HasRelationInput(relation_inputs, input) || HasConditionalInput(operation, mappings, input);
}

} // namespace

CompiledRequiredInputReference::CompiledRequiredInputReference(CompiledRequiredInputKind kind_p, std::string id_p)
    : kind(kind_p), id(std::move(id_p)) {
	ValidateReferenceKind(kind);
	if (!IsSelectorInputIdentifier(id)) {
		throw std::invalid_argument("compiled required-input reference contains an invalid identifier");
	}
}

CompiledRequiredInputKind CompiledRequiredInputReference::Kind() const {
	return kind;
}

const std::string &CompiledRequiredInputReference::Id() const {
	return id;
}

CompiledOperationSelector::CompiledOperationSelector() : priority(0), legacy_compatibility_bridge(true) {
}

CompiledOperationSelector::CompiledOperationSelector(std::vector<std::string> required_inputs_p,
                                                     std::vector<std::vector<std::string>> any_input_sets_p,
                                                     std::vector<std::string> forbidden_inputs_p,
                                                     std::int32_t priority_p)
    : required_inputs(std::move(required_inputs_p)), any_input_sets(std::move(any_input_sets_p)),
      forbidden_inputs(std::move(forbidden_inputs_p)), priority(priority_p), legacy_compatibility_bridge(true) {
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

CompiledOperationSelector::CompiledOperationSelector(
    std::vector<CompiledRequiredInputReference> required_input_references_p)
    : required_input_references(NormalizeRequiredReferences(required_input_references_p)), priority(0),
      legacy_compatibility_bridge(false) {
}

const std::vector<CompiledRequiredInputReference> &CompiledOperationSelector::RequiredInputReferences() const {
	return required_input_references;
}

bool CompiledOperationSelector::IsLegacyCompatibilityBridge() const {
	return legacy_compatibility_bridge;
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
	for (const auto &reference : operation.selector.RequiredInputReferences()) {
		const bool represented = reference.Kind() == CompiledRequiredInputKind::RELATION_INPUT
		                             ? HasRelationInput(relation_inputs, reference.Id())
		                             : HasConditionalInput(operation, mappings, reference.Id());
		if (!represented) {
			throw std::invalid_argument("compiled operation selector references a missing or wrong-kind input");
		}
	}
	if (!operation.selector.IsLegacyCompatibilityBridge()) {
		return;
	}
	// Temporary bridge for native/controlled fixtures. Untagged strings are
	// accepted only here until Semantics migrates those fixtures.
	const auto validate = [&operation, &relation_inputs, &mappings](const std::vector<std::string> &inputs) {
		for (const auto &input : inputs) {
			if (!LegacySelectorInputIsRepresentable(operation, relation_inputs, mappings, input)) {
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
