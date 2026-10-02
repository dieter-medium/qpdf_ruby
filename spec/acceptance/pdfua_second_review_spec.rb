# frozen_string_literal: true

# Regressions from the second review of PR #3 (UnusualInputPdfs and EdgeCasePdfs fixtures).
RSpec.describe "PDF/UA fixes on unusual input" do
  def open_pdf(bytes) = QpdfRuby::Document.from_memory(bytes)

  def reopened(doc) = QpdfRuby::Document.from_memory(doc.to_memory)

  def xmp_values(doc, xpath)
    xmp = Nokogiri::XML(doc.metadata.sub(/\A<\?xpacket[^>]*\?>/, ""), &:strict)
    xmp.xpath(xpath, "dc" => "http://purl.org/dc/elements/1.1/", "pdfuaid" => "http://www.aiim.org/pdfua/ns/id/",
                     "rdf" => "http://www.w3.org/1999/02/22-rdf-syntax-ns#").map(&:text)
  end

  context "with XMP that binds the RDF namespace to another prefix" do
    let(:doc) { open_pdf(EdgeCasePdfs.with_xmp(EdgeCasePdfs.xmp_packet(rdf: "r"))) }

    it "adds the identification in that namespace" do
      doc.add_pdfua_identification(title: "A")

      expect(xmp_values(reopened(doc), "//pdfuaid:part")).to eq(["1"])
    end

    it "adds the title in that namespace" do
      doc.add_pdfua_identification(title: "A")

      expect(xmp_values(reopened(doc), "//dc:title/rdf:Alt/rdf:li")).to eq(["A"])
    end
  end

  context "with XMP metadata that has no RDF element" do
    let(:xmp) { "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"></x:xmpmeta>" }
    let(:doc) { open_pdf(EdgeCasePdfs.with_xmp(xmp)) }

    it "reports why the file is not identified instead of raising" do
      expect(doc.apply_pdfua_fixes(title: "A")[:unidentified_reason]).to include("no closing RDF element")
    end

    it "leaves the metadata as it was" do
      doc.apply_pdfua_fixes(title: "A")

      expect(doc.metadata).to eq(xmp)
    end
  end

  context "without a structure tree" do
    let(:doc) { open_pdf(EdgeCasePdfs.with_page_content("0 0 m 10 10 l S\n")) }

    it "does not claim the file is tagged" do
      doc.add_pdfua_identification(title: "A")

      expect(doc.to_memory).not_to match(%r{/Marked\s+true})
    end

    it "does not identify it as PDF/UA" do
      doc.add_pdfua_identification(title: "A")

      expect(doc.metadata).to be_nil
    end

    it "says why in the report" do
      expect(doc.apply_pdfua_fixes(title: "A")[:unidentified_reason]).to include("no structure tree")
    end
  end

  context "with a tagged, identified file" do
    it "reports no reason" do
      doc = open_pdf(EdgeCasePdfs.with_pdfua_xmp_without_title)

      expect(doc.apply_pdfua_fixes(title: "A")[:unidentified_reason]).to be_nil
    end
  end

  context "with a Figure drawn entirely outside the crop box" do
    it "gets the crop box, not an inverted box" do
      doc = open_pdf(UnusualInputPdfs.with_figure_outside_crop_box)
      doc.ensure_bbox

      expect(reopened(doc).show_structure).to match(/<Figure [^>]*BBox="\[0, 0, 200, 200\]"/)
    end
  end

  context "with an untagged Form XObject whose text contains the letters BDC" do
    let(:doc) { open_pdf(UnusualInputPdfs.with_form("BT /F1 12 Tf (BDC and BMC) Tj ET\n")) }

    it "treats the form as untagged decoration" do
      expect(doc.untagged_content).to include(xobjects: 1, texts: 0)
    end
  end

  context "with a list item whose content is on another page" do
    let(:doc) { open_pdf(UnusualInputPdfs.with_list_item_on_two_pages) }

    it "wraps the content in an LBody" do
      expect(doc.wrap_list_bodies).to eq(1)
    end

    it "points the other page's ParentTree entry at the LBody" do
      doc.wrap_list_bodies

      expect(reopened(doc).parent_tree_mismatches).to eq(0)
    end
  end

  context "with an object reference to something that is not a dictionary" do
    let(:doc) { open_pdf(UnusualInputPdfs.with_broken_object_reference) }

    it "leaves the list item as it is" do
      expect(doc.wrap_list_bodies).to eq(0)
    end

    it "does not make QPDF warn" do
      expect { doc.wrap_list_bodies }.not_to output.to_stderr_from_any_process
    end
  end
end
