# frozen_string_literal: true

require "rake"
require "tmpdir"
require "yaml"

load File.expand_path("../../tasks/release_credentials_check.rake", __dir__)

# Only the module is exercised - no task is invoked, nothing is built or pushed.
RSpec.describe ReleaseCredentialsCheck do
  let(:token) { "rubygems_0123456789abcdef" }
  let(:env_with_token) { { "GEM_HOST_API_KEY" => token } }
  let(:missing_file) { File.join(Dir.mktmpdir, "credentials") }

  def credentials_file(content)
    File.join(Dir.mktmpdir, "credentials").tap do |path|
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

  describe ".print_banner" do
    it "names the environment token as the credential in use" do
      expect { described_class.print_banner(nil, [], env: env_with_token, path: missing_file) }
        .to output(/token RubyGems will push with:  GEM_HOST_API_KEY \(environment\)/).to_stdout
    end

    it "says a configured gem.push_key is ignored" do
      expect { described_class.print_banner("mine", ["mine"], env: env_with_token, path: missing_file) }
        .to output(/gem.push_key Bundler will use: mine - ignored, GEM_HOST_API_KEY wins/).to_stdout
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
