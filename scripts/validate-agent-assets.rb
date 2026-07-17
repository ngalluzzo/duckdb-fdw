#!/usr/bin/env ruby

require "yaml"

root = File.expand_path("..", __dir__)
failures = []
asset_paths = [__FILE__]

agents_path = File.join(root, "AGENTS.md")
failures << "AGENTS.md is missing or empty" unless File.size?(agents_path)
asset_paths << agents_path if File.file?(agents_path)

skill_paths = Dir[File.join(root, ".agents", "skills", "*", "SKILL.md")].sort
failures << "no repository skills found" if skill_paths.empty?
discovered_skills = []

skill_paths.each do |skill_path|
  asset_paths << skill_path
  skill_dir = File.dirname(skill_path)
  expected_name = File.basename(skill_dir)
  raw = File.read(skill_path)
  match = raw.match(/\A---\n(.*?)\n---\n(.+)\z/m)

  unless match
    failures << "#{skill_path}: malformed or empty frontmatter/body"
    next
  end

  begin
    frontmatter = YAML.safe_load(match[1], permitted_classes: [], aliases: false)
  rescue Psych::Exception => error
    failures << "#{skill_path}: invalid YAML: #{error.message}"
    next
  end

  unless frontmatter.is_a?(Hash) && frontmatter.keys.sort == %w[description name]
    failures << "#{skill_path}: frontmatter must contain only name and description"
    next
  end

  name = frontmatter["name"]
  description = frontmatter["description"]
  discovered_skills << name if name.is_a?(String)
  failures << "#{skill_path}: name must match #{expected_name}" unless name == expected_name
  failures << "#{skill_path}: invalid skill name" unless name&.match?(/\A[a-z0-9-]{1,63}\z/)
  failures << "#{skill_path}: description is empty" unless description.is_a?(String) && !description.strip.empty?
  failures << "#{skill_path}: placeholder text remains" if raw.match?(/\bTODO\b|\[TODO/)

  metadata_path = File.join(skill_dir, "agents", "openai.yaml")
  unless File.file?(metadata_path)
    failures << "#{metadata_path}: missing"
    next
  end
  asset_paths << metadata_path

  begin
    metadata = YAML.safe_load(File.read(metadata_path), permitted_classes: [], aliases: false)
  rescue Psych::Exception => error
    failures << "#{metadata_path}: invalid YAML: #{error.message}"
    next
  end

  interface = metadata.is_a?(Hash) ? metadata["interface"] : nil
  unless interface.is_a?(Hash)
    failures << "#{metadata_path}: interface mapping is missing"
    next
  end

  display_name = interface["display_name"]
  short_description = interface["short_description"]
  default_prompt = interface["default_prompt"]
  failures << "#{metadata_path}: display_name is missing" unless display_name.is_a?(String) && !display_name.empty?
  unless short_description.is_a?(String) && (25..64).cover?(short_description.length)
    failures << "#{metadata_path}: short_description must be 25-64 characters"
  end
  unless default_prompt.is_a?(String) && default_prompt.include?("$#{expected_name}")
    failures << "#{metadata_path}: default_prompt must mention $#{expected_name}"
  end
end

if File.file?(agents_path)
  required_skills = File.read(agents_path).scan(/\$([a-z0-9-]+)/).flatten.uniq
  (required_skills - discovered_skills).each do |name|
    failures << "AGENTS.md references missing repository skill $#{name}"
  end
end

asset_paths.uniq.each do |path|
  File.foreach(path).with_index(1) do |line, number|
    failures << "#{path}:#{number}: trailing whitespace" if line.match?(/[ \t]+(?:\r?\n)?\z/)
  end
end

unless failures.empty?
  warn failures.join("\n")
  exit 1
end

puts "Validated AGENTS.md and #{skill_paths.length} repository skills."
