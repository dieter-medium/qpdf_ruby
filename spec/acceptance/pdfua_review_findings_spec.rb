# frozen_string_literal: true

# Regressions from the review of PR #3: each case is a small synthetic PDF (EdgeCasePdfs), so
# the failure it guards against cannot hide behind a real fixture that happens not to trigger it.
RSpec.describe "PDF/UA fixes on edge cases" do
  def open_pdf(bytes) = QpdfRuby::Document.from_memory(bytes)

  def reopened(doc) = QpdfRuby::Document.from_memory(doc.to_memory)

  context "with list items whose page and Form XObject content share MCID 0" do
    let(:doc) { open_pdf(EdgeCasePdfs.with_list_items_sharing_mcid) }

    it "starts out with a consistent ParentTree" do
      expect(doc.parent_tree_mismatches).to eq(0)
    end

    it "points only the form's ParentTree entry at the new LBody" do
      doc.wrap_list_bodies

      expect(reopened(doc).parent_tree_mismatches).to eq(0)
    end

    it "wraps both items" do
      expect(doc.wrap_list_bodies).to eq(2)
    end
  end

  context "with a graphics state change inside an untagged path" do
    let(:doc) { open_pdf(EdgeCasePdfs.with_page_content("0 0 m 10 10 l 2 w S\n")) }

    it "counts the path once" do
      expect(doc.untagged_content).to include(paths: 1, total: 1)
    end

    it "wraps the whole path, the state change included, in one artifact" do
      doc.mark_untagged_content_as_artifacts

      expect(EdgeCasePdfs.content_streams(doc.to_memory)).to match(%r{/Artifact BMC\s+0 0 m\s+10 10 l\s+2 w\s+S\s+EMC})
    end

    it "leaves nothing untagged afterwards" do
      doc.mark_untagged_content_as_artifacts

      expect(reopened(doc).untagged_content[:total]).to eq(0)
    end
  end

  context "with a graphics state change inside a tagged path" do
    let(:content) { "/P << /MCID 0 >> BDC 0 0 m 10 10 l 2 w S EMC\n" }
    let(:doc) { open_pdf(EdgeCasePdfs.with_page_content(content)) }

    it "counts nothing" do
      expect(doc.untagged_content[:total]).to eq(0)
    end

    it "leaves the content as it is" do
      doc.mark_untagged_content_as_artifacts

      expect(EdgeCasePdfs.content_streams(doc.to_memory)).to eq(content)
    end
  end

  context "with a graphics state change inside a clipping path" do
    let(:content) { "0 0 10 10 re W 2 w n\n" }
    let(:doc) { open_pdf(EdgeCasePdfs.with_page_content(content)) }

    it "counts nothing" do
      expect(doc.untagged_content[:total]).to eq(0)
    end

    it "leaves the content as it is" do
      doc.mark_untagged_content_as_artifacts

      expect(EdgeCasePdfs.content_streams(doc.to_memory)).to eq(content)
    end
  end

  context "with XMP that identifies the file as PDF/UA-1 but has no title" do
    let(:doc) { open_pdf(EdgeCasePdfs.with_pdfua_xmp_without_title) }

    def titles(doc)
      xmp = Nokogiri::XML(doc.metadata.sub(/\A<\?xpacket[^>]*\?>/, ""), &:strict)
      xmp.xpath("//dc:title//rdf:li", "dc" => "http://purl.org/dc/elements/1.1/",
                                      "rdf" => "http://www.w3.org/1999/02/22-rdf-syntax-ns#").map(&:text)
    end

    it "adds the given title as valid XMP" do
      doc.add_pdfua_identification(title: "A & B")

      expect(titles(reopened(doc))).to eq(["A & B"])
    end

    it "reports the change" do
      expect(doc.add_pdfua_identification(title: "A & B")).to be(true)
    end

    it "adds nothing the second time" do
      doc.add_pdfua_identification(title: "A & B")

      expect(doc.add_pdfua_identification(title: "A & B")).to be(false)
    end

    it "leaves the metadata as it was the second time" do
      doc.add_pdfua_identification(title: "A & B")
      before = doc.metadata
      doc.add_pdfua_identification(title: "A & B")

      expect(doc.metadata).to eq(before)
    end

    it "does not identify the file twice" do
      doc.add_pdfua_identification(title: "A & B")

      expect(doc.metadata.scan("pdfuaid:part").size).to eq(2)
    end
  end

  context "with XMP that identifies the file in the attribute form" do
    let(:doc) { open_pdf(EdgeCasePdfs.with_pdfua_xmp_without_title('pdfuaid:part="1"')) }

    it "does not add a second identification" do
      doc.add_pdfua_identification(title: "A")

      expect(doc.metadata.scan("pdfuaid:part").size).to eq(1)
    end
  end

  [false, true].each do |kids|
    context "with a link to a string destination in a name tree#{" split into /Kids" if kids}" do
      let(:doc) { open_pdf(EdgeCasePdfs.with_named_string_destination(kids: kids)) }

      it "describes the link by the page it opens" do
        doc.describe_links

        expect(doc.links.map { |link| link[:contents] }).to eq(["Page 2"])
      end
    end
  end

  context "with a Figure in a Form XObject whose MCID the page's content uses too" do
    let(:doc) { open_pdf(EdgeCasePdfs.with_figure_in_form_sharing_mcid) }

    it "gives the Figure the bounds of its own, transformed content" do
      doc.ensure_bbox

      expect(reopened(doc).show_structure).to match(/<Figure [^>]*BBox="\[100, 100, 120, 120\]"/)
    end
  end
end
