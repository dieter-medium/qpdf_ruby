# frozen_string_literal: true

require "open3"
require "tmpdir"

# Real Chromium output (HeadlessChrome 154, Page.printToPDF with generateTaggedPDF and
# generateDocumentOutline - see spec/fixtures/chromium/README.md) made PDF/UA-1 conformant.
RSpec.describe "Making Chromium's tagged PDFs PDF/UA-1 conformant" do
  def fixture(name) = File.expand_path("../fixtures/chromium/#{name}", __dir__)

  def open_fixture(name) = QpdfRuby::Document.new(fixture(name))

  def fixed(name, **options)
    doc = open_fixture(name)
    doc.apply_pdfua_fixes(**options)
    QpdfRuby::Document.from_memory(doc.to_memory)
  end

  def structure(doc) = Nokogiri::XML("<root>#{doc.show_structure}</root>", &:noblanks)

  def mcids(doc) = doc.show_structure.scan(/\[MCID: (\d+)\]/).flatten.tally

  %w[chromium_print_cases.pdf modern_cv.pdf].each do |name|
    context "with #{name}" do
      it "starts out with content Chromium left untagged" do
        expect(open_fixture(name).untagged_content[:total]).to be_positive
      end

      it "leaves no content untagged" do
        expect(fixed(name).untagged_content[:total]).to eq(0)
      end

      it "keeps every piece of tagged content" do
        expect(mcids(fixed(name))).to eq(mcids(open_fixture(name)))
      end

      it "describes every link" do
        expect(fixed(name).links.map { |link| link[:contents] }).to all(be_a(String).and(satisfy { |text| !text.empty? }))
      end

      it "keeps every ParentTree entry pointing at the element that holds the content" do
        expect(fixed(name).parent_tree_mismatches).to eq(0)
      end

      it "gives list items only labels and bodies" do
        kids = structure(fixed(name)).xpath("//LI/*").map(&:name).uniq

        expect(kids - %w[Lbl LBody]).to be_empty
      end

      it "identifies the file as PDF/UA-1" do
        expect(fixed(name).metadata).to include("<pdfuaid:part>1</pdfuaid:part>")
      end

      it "changes nothing when applied a second time" do
        doc = fixed(name)
        report = doc.apply_pdfua_fixes

        expect(report).to include(artifacts: include(total: 0), links: 0, list_bodies: 0, roles: 0, figure_groups: 0,
                                  identified: false)
      end
    end
  end

  context "with chromium_print_cases.pdf" do
    let(:name) { "chromium_print_cases.pdf" }

    it "maps Chromium's PDF 2.0 tags to standard types" do
      expect(fixed(name).role_map).to include("Strong" => "Span", "Em" => "Span")
    end

    it "turns the Figure Chromium makes of an HTML <figure> into a Div around the real figure" do
      figure_group = structure(fixed(name)).at_xpath("//Div[Figure and Caption]")

      expect(figure_group).not_to be_nil
    end

    it "leaves no figure without alternative text" do
      expect(fixed(name).figures_without_alt).to eq(0)
    end

    it "describes an internal link by the page it opens" do
      expect(fixed(name).links.find { |link| link[:uri].nil? }[:contents]).to eq("Page 3")
    end

    it "describes a link with the text given for its target" do
      doc = fixed(name, link_texts: { "mailto:jana@example.com" => "E-mail Jana" })

      expect(doc.links.find { |link| link[:uri] == "mailto:jana@example.com" }[:contents]).to eq("E-mail Jana")
    end

    it "takes the document title for the metadata from the given title" do
      expect(fixed(name, title: "Lebenslauf & CV").metadata).to include("Lebenslauf &amp; CV")
    end
  end

  context "with a Form XObject that carries tagged content (synthetic)" do
    let(:doc) { QpdfRuby::Document.from_memory(MinimalPdf.with_tagged_form_xobject) }

    it "counts the untagged rectangle and the decorative form, not the tagged form" do
      expect(doc.untagged_content).to include(paths: 1, xobjects: 1, total: 2)
    end

    it "wraps only the untagged content" do
      report = doc.apply_pdfua_fixes

      expect(report[:artifacts]).to include(paths: 1, xobjects: 1)
    end

    it "leaves nothing untagged afterwards" do
      doc.apply_pdfua_fixes

      expect(QpdfRuby::Document.from_memory(doc.to_memory).untagged_content[:total]).to eq(0)
    end
  end

  # Skipped where veraPDF is not installed (bin/install-verapdf.sh) - unless REQUIRE_VERAPDF=1, as
  # in CI, where a missing veraPDF must fail the run rather than quietly drop the conformance check.
  describe "veraPDF", :verapdf do
    def without_verapdf
      message = "veraPDF is not installed (bin/install-verapdf.sh)"
      ENV["REQUIRE_VERAPDF"] == "1" ? raise(message) : skip(message)
    end

    before do
      _, status = Open3.capture2e("verapdf", "--version")
      without_verapdf unless status.success?
    rescue Errno::ENOENT
      without_verapdf
    end

    %w[chromium_print_cases.pdf modern_cv.pdf].each do |name|
      it "finds #{name} PDF/UA-1 compliant after the fixes" do
        Dir.mktmpdir do |dir|
          path = File.join(dir, name)
          File.binwrite(path, fixed(name).to_memory)
          report, = Open3.capture2e("verapdf", "--flavour", "ua1", "--format", "xml", path)

          expect(report).to include('isCompliant="true"')
        end
      end
    end
  end
end
