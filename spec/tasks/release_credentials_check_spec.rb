# frozen_string_literal: true

require "rake"
require "tmpdir"
require "yaml"

load File.expand_path("../../tasks/release_credentials_check.rake", __dir__)

# Only the module is exercised - no task is invoked, nothing is built or pushed.
RSpec.describe ReleaseCredentialsCheck do
  let(:token) { "rubygems_0123456789abcdef" }
  let(:env_with_token) { { "GEM_HOST_API_KEY" => token } }
  let(:dir) { Dir.mktmpdir }
  let(:missing_file) { File.join(dir, "credentials") }

  after { FileUtils.remove_entry(dir) }

  def credentials_file(content)
    File.join(dir, "credentials").tap do |path|
      File.write(path, content)
      File.chmod(0o600, path)
    end
  end

  describe ".problem" do
    it "lets an environment token through without a credentials file" do
      expect(described_class.problem(nil, [], env: env_with_token, path: missing_file)).to be_nil
    end

    it "lets an environment token through when gem.push_key names a different file key" do
      expect(described_class.problem("other", ["mine"], env: env_with_token, path: missing_file)).to be_nil
    end

    it "stops an empty environment token" do
      expect(described_class.problem(nil, ["rubygems_api_key"], env: { "GEM_HOST_API_KEY" => "" }))
        .to include("GEM_HOST_API_KEY is set but empty")
    end

    it "still stops a configured key the credentials file does not have" do
      expect(described_class.problem("other", ["mine"], env: {}, path: "/x/credentials"))
        .to include("configured key 'other' is not among /x/credentials's keys")
    end

    it "still stops when there is no credential at all" do
      expect(described_class.problem(nil, [], env: {}, path: "/x/credentials")).to include("/x/credentials has no keys")
    end
  end

  describe ".problem without gem.push_key" do
    let(:host) { "https://rubygems.org" }

    it "lets a key for the push host through, as RubyGems uses it" do
      expect(described_class.problem(nil, [host], env: {}, path: "/x/credentials", host: host)).to be_nil
    end

    it "stops when the only key is for another host" do
      expect(described_class.problem(nil, ["https://gems.example"], env: {}, path: "/x/credentials", host: host))
        .to include("neither a key for https://rubygems.org nor a default 'rubygems_api_key'")
    end
  end

  describe ".file_key_in_use" do
    it "prefers the push host's key to the default" do
      expect(described_class.file_key_in_use(nil, %w[rubygems_api_key https://rubygems.org], "https://rubygems.org"))
        .to eq("https://rubygems.org")
    end

    it "prefers a configured gem.push_key to the push host's key" do
      expect(described_class.file_key_in_use("mine", %w[mine https://rubygems.org], "https://rubygems.org")).to eq("mine")
    end
  end

  describe ".push_host" do
    it "is the gemspec's allowed_push_host" do
      expect(described_class.push_host({})).to eq("https://rubygems.org")
    end

    it "falls back to RUBYGEMS_HOST" do
      expect(described_class.push_host({ "RUBYGEMS_HOST" => "https://gems.example" }, allowed: nil))
        .to eq("https://gems.example")
    end

    it "ignores an empty RUBYGEMS_HOST" do
      expect(described_class.push_host({ "RUBYGEMS_HOST" => "" }, allowed: nil)).to eq(Gem.host)
    end
  end

  describe ".print_banner" do
    it "names the environment token as the credential in use" do
      expect { described_class.print_banner(nil, [], env: env_with_token, path: missing_file) }
        .to output(/token RubyGems will push with:  GEM_HOST_API_KEY \(environment\)/).to_stdout
    end

    it "says a configured gem.push_key is ignored" do
      expect { described_class.print_banner("mine", ["mine"], env: env_with_token, path: missing_file) }
        .to output(/gem.push_key Bundler will use: mine - ignored, GEM_HOST_API_KEY wins/).to_stdout
    end

    it "names the push host's key as the one in use" do
      expect do
        described_class.print_banner(nil, ["https://rubygems.org"], env: {}, path: missing_file,
                                                                    host: "https://rubygems.org")
      end.to output(%r{credentials key in use:        https://rubygems.org}).to_stdout
    end

    it "never prints the token" do
      expect { described_class.print_banner("mine", ["mine"], env: env_with_token, path: missing_file) }
        .not_to output(/#{token}/o).to_stdout
    end
  end

  describe ".available_keys" do
    it "reads the key names the way RubyGems does, without their values" do
      path = credentials_file(":rubygems_api_key: secret-a\n:mine: secret-b\n")

      expect(described_class.available_keys(path)).to eq(%w[rubygems_api_key mine])
    end

    it "finds no keys in a missing file" do
      expect(described_class.available_keys(missing_file)).to eq([])
    end
  end

  describe ".credentials_path" do
    it "is the path RubyGems itself uses" do
      expect(described_class.credentials_path).to eq(Gem.configuration.credentials_path)
    end
  end
end
