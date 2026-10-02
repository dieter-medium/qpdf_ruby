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

  # A link text's #to_s is Ruby code: it can reopen the very document being changed (initialize
  # frees the old handle). The work must land in the document that is open afterwards.
  def reopening_text(doc, path, text)
    Object.new.tap do |value|
      value.define_singleton_method(:to_s) do
        doc.send(:initialize, path)
        text
      end
    end
  end

  it "describes the links of the document a link text reopened" do
    doc = described_class.new(fixture)
    doc.describe_links({ "mailto:nobody@example.com" => reopening_text(doc, other, "Mail") })

    expect(doc.links.map { |link| link[:contents] }).to all(be_a(String))
  end

  it "applies the fixes to the document a link text reopened" do
    doc = described_class.new(fixture)
    doc.apply_pdfua_fixes(link_texts: { "mailto:nobody@example.com" => reopening_text(doc, other, "Mail") })

    expect(doc.metadata).to include("<pdfuaid:part>1</pdfuaid:part>")
  end

  it "raises QpdfRuby::Error for a file QPDF cannot read" do
    expect { described_class.from_memory("%PDF-1.7\n garbage") }.to raise_error(QpdfRuby::Error)
  end
end
