# frozen_string_literal: true

require "bundler/gem_tasks"
require "rspec/core/rake_task"

RSpec::Core::RakeTask.new(:spec)

require "rubocop/rake_task"

RuboCop::RakeTask.new

require "rake/extensiontask"

desc "Build the gem including native extensions"
task build: :compile

GEMSPEC = Gem::Specification.load("qpdf_ruby.gemspec")

Rake::ExtensionTask.new("qpdf_ruby", GEMSPEC) do |ext|
  ext.lib_dir = "lib/qpdf_ruby"
end

Dir.glob("tasks/*.rake").each { |r| load r }

task default: %i[clobber compile spec rubocop]
