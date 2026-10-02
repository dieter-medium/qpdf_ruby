# frozen_string_literal: true

# The Ruby-facing argument handling of QpdfRuby::Document (ext/qpdf_ruby/qpdf_ruby.cpp).
RSpec.describe QpdfRuby::Document do
  let(:fixture) { File.expand_path("../fixtures/chromium/chromium_print_cases.pdf", __dir__) }
  let(:other) { File.expand_path("../fixtures/chromium/modern_cv.pdf", __dir__) }

  it "opens the new file when initialize is called again" do
    doc = described_class.new(fixture)
    doc.send(:initialize, other)

    expect(doc.to_memory).to eq(described_class.new(other).to_memory)
  end

  it "rejects a file name that is not a String" do
    expect { described_class.new(42) }.to raise_error(TypeError)
  end

  it "rejects a title that is not a String" do
    expect { described_class.new(fixture).apply_pdfua_fixes(title: 42) }.to raise_error(TypeError)
  end

  it "stays usable after a rejected argument" do
    doc = described_class.new(fixture)
    begin
      doc.apply_pdfua_fixes(link_texts: "not a hash")
    rescue TypeError
      nil
    end

    expect(doc.untagged_content[:total]).to be_positive
  end

  it "takes link texts as any object with #to_s" do
    doc = described_class.new(fixture)
    doc.describe_links({ "mailto:jana@example.com" => :Mail })

    expect(doc.links.find { |link| link[:uri] == "mailto:jana@example.com" }[:contents]).to eq("Mail")
  end

  it "raises QpdfRuby::Error for a file QPDF cannot read" do
    expect { described_class.from_memory("%PDF-1.7\n garbage") }.to raise_error(QpdfRuby::Error)
  end
end
