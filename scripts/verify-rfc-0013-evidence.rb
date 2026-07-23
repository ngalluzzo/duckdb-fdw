#!/usr/bin/env ruby
# frozen_string_literal: true

require "base64"
require "digest"
require "json"
require "pathname"
require "psych"

class VerificationFailure < StandardError
end

ROOT = Pathname.new(__dir__).join("..").expand_path
RFC = ROOT.join("docs/rfcs/0013-define-connector-package-v1-contract.md")
EVIDENCE = ROOT.join("docs/rfcs/evidence/0013")
GITHUB = EVIDENCE.join("github")
MANIFEST = EVIDENCE.join("evidence-manifest.json")
U64_MAX = (1 << 64) - 1

def fail_verification(message)
  raise VerificationFailure, message
end

def sha256(path_or_bytes, bytes: false)
  data = bytes ? path_or_bytes : File.binread(path_or_bytes)
  "sha256.#{Digest::SHA256.hexdigest(data)}"
end

def normalize_yaml_node(node, path)
  fail_verification("#{path}: YAML anchors are prohibited") if node.respond_to?(:anchor) && node.anchor
  fail_verification("#{path}: explicit YAML tag is prohibited") if node.respond_to?(:tag) && node.tag && !node.tag.empty?
  case node
  when Psych::Nodes::Mapping
    result = {}
    node.children.each_slice(2) do |key_node, value_node|
      key = normalize_yaml_node(key_node, path)
      fail_verification("#{path}: mapping key is not a scalar") unless key.is_a?(String)
      fail_verification("#{path}: duplicate mapping key #{key.inspect}") if result.key?(key)
      result[key] = normalize_yaml_node(value_node, "#{path}.#{key}")
    end
    result
  when Psych::Nodes::Sequence
    node.children.each_with_index.map { |child, index| normalize_yaml_node(child, "#{path}[#{index}]") }
  when Psych::Nodes::Scalar
    prohibited = [Psych::Nodes::Scalar::SINGLE_QUOTED, Psych::Nodes::Scalar::LITERAL,
                  Psych::Nodes::Scalar::FOLDED]
    fail_verification("#{path}: prohibited YAML scalar style") if prohibited.include?(node.style)
    node.value
  when Psych::Nodes::Alias
    fail_verification("#{path}: YAML aliases are prohibited")
  else
    fail_verification("#{path}: unsupported YAML node #{node.class}")
  end
end

def load_failsafe_yaml(path)
  bytes = File.binread(path)
  fail_verification("#{path}: UTF-8 BOM is prohibited") if bytes.start_with?("\xEF\xBB\xBF".b)
  fail_verification("#{path}: CR line endings are prohibited") if bytes.include?("\r")
  fail_verification("#{path}: NUL is prohibited") if bytes.include?("\0")
  if bytes.each_line.any? { |line| /\A[ ]*\t/.match?(line) }
    fail_verification("#{path}: tabs used as indentation are prohibited")
  end
  stream = Psych.parse_stream(bytes, path.to_s)
  fail_verification("#{path}: expected one YAML document") unless stream.children.length == 1
  document = stream.children.fetch(0)
  fail_verification("#{path}: empty YAML document") unless document.root
  normalize_yaml_node(document.root, "$")
rescue Psych::SyntaxError => error
  fail_verification("#{path}: malformed YAML: #{error.message}")
end

class JsonSchemaSubset
  def initialize(root_schema)
    @root = root_schema
  end

  def validate!(value, label)
    check(@root, value, "$", label)
  end

  private

  def valid?(schema, value, path, label)
    check(schema, value, path, label)
    true
  rescue VerificationFailure
    false
  end

  def resolve(reference)
    fail_verification("unsupported schema reference #{reference}") unless reference.start_with?("#/")
    reference.delete_prefix("#/").split("/").reduce(@root) do |node, token|
      node.fetch(token.gsub("~1", "/").gsub("~0", "~"))
    end
  end

  def check(schema, value, path, label)
    return if schema.empty?
    check(resolve(schema.fetch("$ref")), value, path, label) if schema.key?("$ref")

    if schema.key?("oneOf")
      matches = schema.fetch("oneOf").count { |candidate| valid?(candidate, value, path, label) }
      fail_verification("#{label} #{path}: expected exactly one schema alternative, got #{matches}") unless matches == 1
    end
    if schema.key?("anyOf")
      matches = schema.fetch("anyOf").any? { |candidate| valid?(candidate, value, path, label) }
      fail_verification("#{label} #{path}: no schema alternative matched") unless matches
    end
    if schema.key?("not") && valid?(schema.fetch("not"), value, path, label)
      fail_verification("#{label} #{path}: prohibited schema matched")
    end
    if schema.key?("const") && value != schema.fetch("const")
      fail_verification("#{label} #{path}: expected #{schema.fetch('const').inspect}, got #{value.inspect}")
    end
    if schema.key?("enum") && !schema.fetch("enum").include?(value)
      fail_verification("#{label} #{path}: value #{value.inspect} is outside the closed enum")
    end

    case schema["type"]
    when "object"
      fail_verification("#{label} #{path}: expected object") unless value.is_a?(Hash)
    when "array"
      fail_verification("#{label} #{path}: expected array") unless value.is_a?(Array)
    when "string"
      fail_verification("#{label} #{path}: expected string") unless value.is_a?(String)
    when nil
      # Structural alternatives may constrain only selected fields.
    else
      fail_verification("#{label} #{path}: verifier does not implement schema type #{schema['type']}")
    end

    if value.is_a?(Hash)
      required = schema.fetch("required", [])
      required.each { |key| fail_verification("#{label} #{path}: missing #{key}") unless value.key?(key) }
      properties = schema.fetch("properties", {})
      properties.each { |key, child| check(child, value[key], "#{path}.#{key}", label) if value.key?(key) }
      extras = value.keys - properties.keys
      additional = schema.fetch("additionalProperties", true)
      fail_verification("#{label} #{path}: unknown fields #{extras.sort.join(', ')}") if additional == false && !extras.empty?
      if additional.is_a?(Hash)
        extras.each { |key| check(additional, value.fetch(key), "#{path}.#{key}", label) }
      end
      value.keys.each { |key| check(schema.fetch("propertyNames"), key, "#{path}.<key>", label) } if schema.key?("propertyNames")
      fail_verification("#{label} #{path}: too few properties") if schema["minProperties"] && value.length < schema["minProperties"]
      fail_verification("#{label} #{path}: too many properties") if schema["maxProperties"] && value.length > schema["maxProperties"]
    elsif value.is_a?(Array)
      fail_verification("#{label} #{path}: too few items") if schema["minItems"] && value.length < schema["minItems"]
      fail_verification("#{label} #{path}: too many items") if schema["maxItems"] && value.length > schema["maxItems"]
      if schema["uniqueItems"] && value.map { |item| JSON.generate(item) }.uniq.length != value.length
        fail_verification("#{label} #{path}: duplicate array item")
      end
      value.each_with_index { |item, index| check(schema.fetch("items"), item, "#{path}[#{index}]", label) } if schema.key?("items")
    elsif value.is_a?(String)
      fail_verification("#{label} #{path}: string is too short") if schema["minLength"] && value.length < schema["minLength"]
      fail_verification("#{label} #{path}: string is too long") if schema["maxLength"] && value.length > schema["maxLength"]
      if schema["pattern"] && !Regexp.new(schema["pattern"]).match?(value)
        fail_verification("#{label} #{path}: string does not match #{schema['pattern']}")
      end
    end
  end
end

def resolve_path(environment, expression)
  alias_name, *segments = expression.split(".")
  value = environment[alias_name]
  segments.each do |segment|
    return nil unless value.is_a?(Hash) && value.key?(segment)
    value = value.fetch(segment)
  end
  value
end

def expand_scope(contract, scope, package)
  specification = contract.fetch("derivation_language").fetch("scope_vocabulary").fetch(scope)
  environments = [{"package" => package, "generation" => {}}]
  specification.fetch("selector").each do |step|
    next if step.key?("synthetic")
    expanded = []
    environments.each do |environment|
      if step.key?("iterate")
        values = resolve_path(environment, step.fetch("iterate")) || []
        fail_verification("coverage selector #{scope} did not resolve an array") unless values.is_a?(Array)
        values.each { |value| expanded << environment.merge(step.fetch("as") => value) }
      else
        fields = resolve_path({"derivation_language" => contract.fetch("derivation_language")}, step.fetch("fields"))
        source = resolve_path(environment, step.fetch("from"))
        fail_verification("coverage field selector #{scope} is invalid") unless fields.is_a?(Array) && source.is_a?(Hash)
        fields.each do |name|
          next unless source.key?(name)
          expanded << environment.merge(step.fetch("as") => {"name" => name, "value" => source.fetch(name)})
        end
      end
    end
    environments = expanded
  end
  environments.select do |environment|
    specification.fetch("where", []).all? do |condition|
      if condition.key?("equals")
        expression, expected = condition.fetch("equals")
        resolve_path(environment, expression) == expected
      elsif condition.key?("present")
        !resolve_path(environment, condition.fetch("present")).nil?
      elsif condition.key?("count_gt")
        expression, count = condition.fetch("count_gt")
        value = resolve_path(environment, expression)
        value.is_a?(Array) && value.length > count
      else
        fail_verification("coverage scope #{scope} contains an unknown condition")
      end
    end
  end.map do |environment|
    specification.fetch("bindings").to_h do |name, expression|
      value = resolve_path(environment, expression)
      fail_verification("coverage binding #{scope}.#{name} did not resolve") unless value.is_a?(String)
      [name, value]
    end
  end
end

def derive_coverage_keys(contract, package)
  keys = []
  contract.fetch("rules").each do |rule|
    expand_scope(contract, rule.fetch("scope"), package).each do |bindings|
      rule.fetch("variants").each do |variant|
        replacements = bindings.merge("variant" => variant)
        key = rule.fetch("template").gsub(/\{([a-z_]+)\}/) { replacements.fetch(Regexp.last_match(1)) }
        fail_verification("derived invalid coverage key #{key}") unless /\A[a-z][a-z0-9_]{0,254}\z/.match?(key)
        keys << key
      end
    end
  end
  diagnostic = contract.fetch("diagnostic_rule")
  diagnostic.fetch("codes").each do |code|
    keys << diagnostic.fetch("template").sub("{lowercase_code}", code.downcase)
  end
  duplicates = keys.group_by(&:itself).select { |_key, values| values.length > 1 }.keys
  fail_verification("duplicate derived coverage keys: #{duplicates.join(', ')}") unless duplicates.empty?
  keys
end

def positive_u64(value, label)
  fail_verification("#{label}: not canonical positive decimal") unless /\A[1-9][0-9]*\z/.match?(value)
  number = Integer(value, 10)
  fail_verification("#{label}: exceeds unsigned 64-bit range") if number > U64_MAX
  number
end

def checked_product(left, right, label)
  fail_verification("#{label}: unsigned 64-bit multiplication overflow") if left > U64_MAX / right
  left * right
end

def checked_sum(left, right, label)
  fail_verification("#{label}: unsigned 64-bit addition overflow") if left > U64_MAX - right
  left + right
end

def verify_numeric_envelopes(connector, relations)
  positive_u64(U64_MAX.to_s, "u64 boundary self-check")
  begin
    positive_u64((U64_MAX + 1).to_s, "u64 one-over self-check")
  rescue VerificationFailure
    # The post-schema range check must reject the longest lexical counterexample.
  else
    fail_verification("u64 one-over self-check was accepted")
  end

  network_limit = positive_u64(connector.fetch("network_policy").fetch("max_response_bytes"), "network max_response_bytes")
  resource_fields = %w[max_response_bytes_per_page max_response_bytes_per_scan max_records_per_page
                       max_records_per_scan max_extracted_string_bytes]
  relations.each do |relation|
    values = resource_fields.to_h { |field| [field, positive_u64(relation.fetch("resources").fetch(field), "#{relation['id']}.#{field}")] }
    if values.fetch("max_response_bytes_per_page") > network_limit
      fail_verification("#{relation['id']}: page response ceiling widens manifest network policy")
    end
    relation.fetch("operations").each do |operation|
      if operation.fetch("request").fetch("protocol") == "rest"
        pagination = operation.fetch("pagination")
        max_pages = pagination.fetch("strategy") == "disabled" ? 1 : positive_u64(pagination.fetch("max_pages_per_scan"), "#{operation['id']}.max_pages")
        if pagination.fetch("strategy") == "link_next"
          positive_u64(pagination.fetch("page_size"), "#{operation['id']}.page_size")
          first = positive_u64(pagination.fetch("first_page"), "#{operation['id']}.first_page")
          increment = positive_u64(pagination.fetch("page_increment"), "#{operation['id']}.page_increment")
          last_delta = checked_product(increment, max_pages - 1, "#{operation['id']}.last_page")
          checked_sum(first, last_delta, "#{operation['id']}.last_page")
        end
      else
        request = operation.fetch("request")
        pagination = request.fetch("pagination")
        max_pages = positive_u64(pagination.fetch("max_pages_per_scan"), "#{operation['id']}.max_pages")
        positive_u64(pagination.fetch("page_size"), "#{operation['id']}.page_size")
        request_body = positive_u64(request.fetch("max_serialized_body_bytes_per_request"), "#{operation['id']}.request_body")
        body_scan = positive_u64(request.fetch("max_serialized_body_bytes_per_scan"), "#{operation['id']}.body_scan")
        fail_verification("#{operation['id']}: GraphQL body scan exceeds checked page envelope") if body_scan > checked_product(request_body, max_pages, "#{operation['id']}.body_envelope")
        document = positive_u64(request.fetch("max_document_bytes"), "#{operation['id']}.document")
        fail_verification("#{operation['id']}: document ceiling exceeds v1 maximum") if document > 65_536
      end
      fail_verification("#{relation['id']}: byte scan exceeds checked page envelope") if values.fetch("max_response_bytes_per_scan") > checked_product(values.fetch("max_response_bytes_per_page"), max_pages, "#{relation['id']}.byte_envelope")
      fail_verification("#{relation['id']}: record scan exceeds checked page envelope") if values.fetch("max_records_per_scan") > checked_product(values.fetch("max_records_per_page"), max_pages, "#{relation['id']}.record_envelope")
    end
  end
end

def main
  rfc = File.binread(RFC)
  manifest = JSON.parse(File.binread(MANIFEST))
  fail_verification("evidence manifest has unknown fields") unless manifest.keys.sort == %w[artifacts manifest verifier]
  fail_verification("wrong evidence manifest contract") unless manifest.fetch("manifest") == "duckdb_api/rfc0013_evidence_v1"
  manifest_digest = sha256(MANIFEST)
  fail_verification("RFC does not cite evidence manifest #{manifest_digest}") unless rfc.include?(manifest_digest)

  expected_files = manifest.fetch("artifacts").keys.sort
  manifest.fetch("artifacts").each do |relative, digest|
    fail_verification("invalid manifest artifact path #{relative.inspect}") unless /\A[a-zA-Z0-9][a-zA-Z0-9_.\/-]*\z/.match?(relative) && !relative.split("/").include?("..")
    fail_verification("invalid manifest artifact digest for #{relative}") unless /\Asha256\.[0-9a-f]{64}\z/.match?(digest)
  end
  actual_files = Dir.glob(EVIDENCE.join("**/*")).select { |path| File.file?(path) }
                    .map { |path| Pathname.new(path).relative_path_from(EVIDENCE).to_s }
                    .reject { |path| path == "evidence-manifest.json" }.sort
  fail_verification("evidence manifest file set differs") unless actual_files == expected_files
  manifest.fetch("artifacts").each do |relative, expected|
    actual = sha256(EVIDENCE.join(relative))
    fail_verification("#{relative}: expected #{expected}, got #{actual}") unless actual == expected
  end
  verifier = manifest.fetch("verifier")
  fail_verification("evidence verifier record has unknown fields") unless verifier.keys.sort == %w[digest path]
  fail_verification("wrong evidence verifier path") unless verifier.fetch("path") == "scripts/verify-rfc-0013-evidence.rb"
  fail_verification("invalid evidence verifier digest") unless /\Asha256\.[0-9a-f]{64}\z/.match?(verifier.fetch("digest"))
  verifier_path = ROOT.join(verifier.fetch("path"))
  fail_verification("verifier identity differs") unless sha256(verifier_path) == verifier.fetch("digest")

  semantic_schema_path = EVIDENCE.join("connector-package-v1.schema.json")
  fixture_schema_path = EVIDENCE.join("fixture-index-v1.schema.json")
  coverage_path = EVIDENCE.join("fixture-coverage-v1.json")
  semantic_schema = JSON.parse(File.binread(semantic_schema_path))
  fixture_schema = JSON.parse(File.binread(fixture_schema_path))
  coverage = JSON.parse(File.binread(coverage_path))
  [semantic_schema_path, fixture_schema_path, coverage_path].each do |path|
    digest = sha256(path)
    fail_verification("RFC does not cite #{path.basename} #{digest}") unless rfc.include?(digest)
  end

  semantic_validator = JsonSchemaSubset.new(semantic_schema)
  fixture_validator = JsonSchemaSubset.new(fixture_schema)
  connector = load_failsafe_yaml(GITHUB.join("connector.yaml"))
  semantic_validator.validate!(connector, "connector.yaml")
  relations = connector.fetch("relations").map do |relation_id|
    path = GITHUB.join("relations/#{relation_id}.yaml")
    relation = load_failsafe_yaml(path)
    semantic_validator.validate!(relation, path.basename.to_s)
    fail_verification("#{path}: relation identity differs") unless relation.fetch("id") == relation_id
    relation
  end

  semantic_paths = ["connector.yaml"] + connector.fetch("relations").map { |id| "relations/#{id}.yaml" }
  framed = semantic_paths.sort.map do |relative|
    bytes = File.binread(GITHUB.join(relative))
    [relative.bytesize].pack("Q>") + relative.b + [bytes.bytesize].pack("Q>") + bytes
  end.join
  package_digest = sha256(framed, bytes: true)
  fail_verification("RFC does not cite package digest #{package_digest}") unless rfc.include?(package_digest)

  fixture_index_path = GITHUB.join("fixtures/index.yaml")
  fixture_index = load_failsafe_yaml(fixture_index_path)
  fixture_validator.validate!(fixture_index, "fixtures/index.yaml")
  fail_verification("fixture package digest differs") unless fixture_index.fetch("package_digest") == package_digest

  referenced_bodies = []
  visit = lambda do |value|
    case value
    when Hash
      referenced_bodies << [value.fetch("body_file"), value.fetch("body_digest")] if value.key?("body_file")
      value.each_value { |child| visit.call(child) }
    when Array
      value.each { |child| visit.call(child) }
    end
  end
  visit.call(fixture_index)
  referenced_bodies.each do |body_file, expected|
    path = GITHUB.join("fixtures", body_file)
    fail_verification("#{body_file}: payload digest differs") unless sha256(path) == expected
    JSON.parse(File.binread(path))
  end
  actual_bodies = Dir.glob(GITHUB.join("fixtures/*")).select { |path| File.file?(path) }
                     .map { |path| File.basename(path) }.reject { |name| name == "index.yaml" }.sort
  body_names = referenced_bodies.map(&:first).sort
  fail_verification("fixture payload file set differs or contains duplicate references") unless actual_bodies == body_names && body_names.uniq == body_names

  golden = load_failsafe_yaml(EVIDENCE.join("graphql-query-golden.yaml"))
  document = Base64.strict_decode64(golden.fetch("document_base64"))
  fail_verification("GraphQL document byte count differs") unless document.bytesize == Integer(golden.fetch("document_bytes"), 10)
  fail_verification("GraphQL document digest differs") unless sha256(document, bytes: true) == golden.fetch("document_digest")
  graphql_case = fixture_index.fetch("cases").find { |entry| entry.fetch("id") == "viewer_repository_metrics_cursor" }
  fail_verification("GraphQL cursor fixture is missing") unless graphql_case
  graphql_case.fetch("execution").fetch("pages").each do |page|
    request = page.fetch("request")
    variables = request.fetch("variables")
    cursor_value = variables.fetch(1).fetch("value")
    cursor = cursor_value.fetch("kind") == "null" ? nil : cursor_value.fetch("value")
    body = JSON.generate("query" => document, "variables" => {"pageSize" => 100, "cursor" => cursor})
    fail_verification("GraphQL serialized body digest differs") unless sha256(body, bytes: true) == request.fetch("serialized_body_digest")
  end

  package = {"relations" => relations}
  required_keys = derive_coverage_keys(coverage, package)
  golden_coverage = coverage.fetch("github_golden")
  required_digest = sha256(required_keys.join("\n") + "\n", bytes: true)
  fail_verification("coverage golden package identity differs") unless package_digest == golden_coverage.fetch("semantic_package_digest")
  fail_verification("coverage key count differs: derived #{required_keys.length}") unless required_keys.length == golden_coverage.fetch("required_key_count")
  fail_verification("coverage key digest differs: derived #{required_digest}") unless required_digest == golden_coverage.fetch("ordered_keys_digest")
  golden_coverage.fetch("selected_keys").each { |key| fail_verification("missing golden coverage key #{key}") unless required_keys.include?(key) }
  claimed_keys = fixture_index.fetch("cases").flat_map { |entry| entry.fetch("covers") }
  fail_verification("fixture coverage claims are duplicated") unless claimed_keys.uniq == claimed_keys
  unknown_claims = claimed_keys - required_keys
  fail_verification("fixture index claims unknown coverage keys: #{unknown_claims.join(', ')}") unless unknown_claims.empty?

  verify_numeric_envelopes(connector, relations)
  puts JSON.generate({"rfc" => "0013", "manifest" => manifest_digest, "package" => package_digest,
                      "artifacts" => manifest.fetch("artifacts").length, "payloads" => referenced_bodies.length,
                      "required_coverage_keys" => required_keys.length, "claimed_decision_keys" => claimed_keys.length})
end

begin
  main
rescue VerificationFailure, JSON::ParserError, KeyError, ArgumentError => error
  warn "RFC 0013 evidence verification failed: #{error.message}"
  exit 1
end
