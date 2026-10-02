# frozen_string_literal: true

require "yaml"

# Works out which rubygems.org credential `rake release` would really push with, and refuses to go on
# when that is not what the repo's configuration says it should be. RubyGems picks the credential in
# this order (Gem::GemcutterUtilities#api_key): GEM_HOST_API_KEY, then the --key Bundler passes for
# gem.push_key, then a key for the push host, then :rubygems_api_key from the credentials file. A
# token value is never read into a message or printed - only where it comes from.
module ReleaseCredentialsCheck
  BUNDLE_CONFIG_PATH = ".bundle/config"
  ENV_TOKEN = "GEM_HOST_API_KEY"
  GEMSPEC_PATH = File.expand_path("../qpdf_ruby.gemspec", __dir__)

  module_function

  # Ask Bundler, exactly as its own release task does (gem_helper.rb: Bundler.settings["gem.push_key"])
  # - never parse .bundle/config for this. Bundler may not be reading that file at all: the official
  # ruby Docker images set BUNDLE_APP_CONFIG=/usr/local/bundle, which silently replaces the repo's
  # .bundle/ as the app config dir. Reading the file directly made this check report a key that
  # `rake release` then did not use.
  def effective_key
    Bundler.settings["gem.push_key"]&.to_s&.downcase
  end

  # What the repo's own .bundle/config asks for - only ever compared against #effective_key.
  def key_on_disk
    return unless File.exist?(BUNDLE_CONFIG_PATH)

    (YAML.load_file(BUNDLE_CONFIG_PATH) || {})["BUNDLE_GEM__PUSH_KEY"]&.to_s&.downcase
  end

  # RubyGems' own answer: ~/.gem/credentials if it exists, else the XDG data home's gem/credentials.
  def credentials_path
    Gem.configuration.credentials_path
  end

  # The key names in the credentials file, read with RubyGems' own loader. Values are dropped.
  def available_keys(path = credentials_path)
    Gem.configuration.load_file(path).keys.map(&:to_s)
  end

  # The host `gem push` sends to, decided as Gem::Commands::PushCommand does: the gemspec's
  # allowed_push_host (Bundler passes it as --host too), else RUBYGEMS_HOST, else Gem.host.
  def push_host(env = ENV, allowed: allowed_push_host)
    rubygems_host = env["RUBYGEMS_HOST"].to_s
    allowed || (rubygems_host.empty? ? nil : rubygems_host) || Gem.host
  end

  def allowed_push_host
    Gem::Specification.load(GEMSPEC_PATH)&.metadata&.[]("allowed_push_host")
  end

  # The credentials file key RubyGems pushes with when no token is in the environment: the
  # configured gem.push_key, else a key named after the push host, else :rubygems_api_key.
  def file_key_in_use(key, keys, host)
    return key if key
    return host if keys.include?(host)

    "rubygems_api_key" if keys.include?("rubygems_api_key")
  end

  # RubyGems uses GEM_HOST_API_KEY whenever it is set - even when empty - ahead of every key below.
  def env_token?(env = ENV)
    env.key?(ENV_TOKEN)
  end

  # Always printed first, in one stream (puts only - mixing in `warn`'s stderr made lines print out
  # of order once a terminal merges the two streams), so the context stays visible above whichever
  # message stops the release.
  def print_banner(key, keys, env: ENV, path: credentials_path, host: push_host(env))
    puts "=== rubygems.org push credentials ==="
    puts "  push host:                     #{host}"
    credential_lines(key, keys, env, host).each { |line| puts line }
    puts "  Bundler app config dir:        #{Bundler.app_config_path}#{" (from BUNDLE_APP_CONFIG)" if ENV["BUNDLE_APP_CONFIG"]}"
    puts "  credentials file:              #{path}#{ignored_note(env)}"
    puts "  credentials keys:              #{keys.empty? ? "(none found)" : keys.join(", ")}"
    puts "======================================"
  end

  def ignored_note(env)
    env_token?(env) ? " - ignored, #{ENV_TOKEN} wins" : ""
  end

  # Which credential is in use: the environment token, or the key RubyGems picks from the file.
  def credential_lines(key, keys, env, host)
    if env_token?(env)
      return ["  token RubyGems will push with:  #{ENV_TOKEN} (environment)",
              "  gem.push_key Bundler will use: #{key ? "#{key}#{ignored_note(env)}" : "(not set)"}"]
    end

    ["  gem.push_key Bundler will use: #{key || "(not set)"}",
     "  credentials key in use:        #{file_key_in_use(key, keys, host) || "(none)"}"]
  end

  # @return [String, nil] why the release must not go on, or nil when it may
  def problem(key, keys, env: ENV, path: credentials_path, host: push_host(env))
    return env_token_problem(env) if env_token?(env)

    ignored_config_problem(key) || credentials_problem(key, keys, path, host)
  end

  # A set token is what RubyGems pushes with, so the file and gem.push_key do not matter - but an
  # empty one would be sent as it is and fail at rubygems.org.
  def env_token_problem(env)
    return unless env[ENV_TOKEN].to_s.strip.empty?

    "#{ENV_TOKEN} is set but empty, and RubyGems would push with it. Unset it to use the credentials file."
  end

  def ignored_config_problem(key)
    wanted = key_on_disk
    return if wanted.nil? || wanted == key

    "#{BUNDLE_CONFIG_PATH} sets gem.push_key '#{wanted}', but Bundler is not reading that file " \
      "(app config dir: #{Bundler.app_config_path}) and would push with '#{key || "the default rubygems_api_key"}'. " \
      "Set BUNDLE_GEM__PUSH_KEY=#{wanted} in the environment, or unset BUNDLE_APP_CONFIG."
  end

  def credentials_problem(key, keys, path, host)
    return "#{path} has no keys at all (missing, empty, or not mounted)." if keys.empty?
    return "configured key '#{key}' is not among #{path}'s keys #{keys.inspect}." if key && !keys.include?(key)
    return if file_key_in_use(key, keys, host)

    "no gem.push_key configured, and #{path} has neither a key for #{host} nor a default 'rubygems_api_key' " \
      "(found: #{keys.inspect})."
  end
end

desc "Show which rubygems.org credential key `rake release`/`gem push` will use"
task :release_credentials_check do
  # Without this, stdout is buffered (not a TTY once piped/captured) while stderr isn't - so
  # `abort`'s message below can reach the terminal before the `puts` lines that logically ran
  # first, once the two streams get merged. Confirmed live: without sync, "release aborted: ..."
  # printed above the diagnostic banner, not after it.
  $stdout.sync = true

  key = ReleaseCredentialsCheck.effective_key
  keys = ReleaseCredentialsCheck.available_keys
  ReleaseCredentialsCheck.print_banner(key, keys)

  problem = ReleaseCredentialsCheck.problem(key, keys)
  abort "release aborted: #{problem}" if problem

  next if key || ReleaseCredentialsCheck.env_token?

  puts "NOTE: no gem.push_key configured - this will push using the default :rubygems_api_key."
end

# A prerequisite of "release:rubygem_push" specifically, not the outer "release" task. Bundler's
# own gem_helper.rb defines release as depending on
# ["build", "release:guard_clean", "release:source_control_push", "release:rubygem_push"], run in
# that listed order, with release:rubygem_push's own action being the actual `rubygem_push(...)`
# call that consumes this credential - attaching to the outer "release" task instead would append
# here via Task#enhance's simple union, running this *after* every one of those, including the
# push itself, which would make it a report on a mistake already made, not a guard against one.
# Attaching to "release:rubygem_push" guarantees Rake resolves this before *that* task's own
# action runs, regardless of how bundler orders its other prerequisites in some other version -
# automatically, regardless of how `rake release` gets invoked (this Makefile's release-shell, a
# bare host checkout, CI), not something you have to remember to check with a separate command.
desc "Push the built gem to rubygems.org (checks credentials first)"
task "release:rubygem_push" => :release_credentials_check
