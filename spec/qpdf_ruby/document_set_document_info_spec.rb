# frozen_string_literal: true

RSpec.describe QpdfRuby::Document, "#set_document_info" do
  def reopened(doc) = described_class.from_memory(doc.to_memory)

  def xmp_values(doc, xpath)
    xmp = Nokogiri::XML(doc.metadata.sub(/\A<\?xpacket[^>]*\?>/, ""), &:strict)
    xmp.xpath(xpath, "xmp" => "http://ns.adobe.com/xap/1.0/", "pdf" => "http://ns.adobe.com/pdf/1.3/").map(&:text)
  end

  let(:creator) { "Formed Success CV (https://www.example.com/?a=1&b=2)" }
  let(:producer) { "Formed Success CV, Beispiel GmbH" }

  context "with an information dictionary and no XMP" do
    let(:doc) { described_class.from_memory(EdgeCasePdfs.with_page_content("")) }

    it "sets the creator" do
      doc.set_document_info(creator: creator, producer: producer)

      expect(reopened(doc).document_info["Creator"]).to eq(creator)
    end

    it "sets the producer" do
      doc.set_document_info(creator: creator, producer: producer)

      expect(reopened(doc).document_info["Producer"]).to eq(producer)
    end

    it "writes the creator tool into a new XMP packet" do
      doc.set_document_info(creator: creator, producer: producer)

      expect(xmp_values(reopened(doc), "//xmp:CreatorTool")).to eq([creator])
    end

    it "writes the producer into a new XMP packet" do
      doc.set_document_info(creator: creator, producer: producer)

      expect(xmp_values(reopened(doc), "//pdf:Producer")).to eq([producer])
    end

    it "reports the change" do
      expect(doc.set_document_info(creator: creator)).to eq(changed: true, xmp_kept: [])
    end

    it "changes nothing the second time" do
      doc.set_document_info(creator: creator, producer: producer)

      expect(doc.set_document_info(creator: creator, producer: producer)).to eq(changed: false, xmp_kept: %w[xmp:CreatorTool pdf:Producer])
    end
  end

  context "with a value not given" do
    let(:doc) { described_class.from_memory(EdgeCasePdfs.with_page_content("")) }

    it "leaves that entry alone" do
      doc.set_document_info(producer: producer)

      expect(reopened(doc).document_info.key?("Creator")).to be(false)
    end

    it "treats an empty value as not given" do
      expect(doc.set_document_info(creator: "", producer: nil)).to eq(changed: false, xmp_kept: [])
    end
  end

  context "with an existing XMP packet" do
    let(:doc) { described_class.from_memory(EdgeCasePdfs.with_xmp(EdgeCasePdfs.xmp_packet("<pdfuaid:part>1</pdfuaid:part>"))) }

    it "adds the creator tool to it" do
      doc.set_document_info(creator: creator)

      expect(xmp_values(reopened(doc), "//xmp:CreatorTool")).to eq([creator])
    end

    it "keeps what it held" do
      doc.set_document_info(creator: creator)

      expect(reopened(doc).metadata).to include("<pdfuaid:part>1</pdfuaid:part>")
    end
  end

  context "with XMP that already names a producer" do
    let(:packet) { EdgeCasePdfs.xmp_packet("<pdf:Producer xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">Skia</pdf:Producer>") }
    let(:doc) { described_class.from_memory(EdgeCasePdfs.with_xmp(packet)) }

    it "keeps that producer and says so" do
      expect(doc.set_document_info(producer: producer)).to eq(changed: true, xmp_kept: ["pdf:Producer"])
    end

    it "still sets the information dictionary's producer" do
      doc.set_document_info(producer: producer)

      expect(reopened(doc).document_info["Producer"]).to eq(producer)
    end
  end

  context "with XMP that is not well-formed" do
    let(:xmp) { "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"></x:xmpmeta>" }
    let(:doc) { described_class.from_memory(EdgeCasePdfs.with_xmp(xmp)) }

    it "leaves the packet as it was" do
      doc.set_document_info(creator: creator)

      expect(doc.metadata).to eq(xmp)
    end

    it "names what it could not write" do
      expect(doc.set_document_info(creator: creator)[:xmp_kept]).to eq(["xmp:CreatorTool"])
    end
  end

  it "rejects an unknown keyword" do
    doc = described_class.from_memory(EdgeCasePdfs.with_page_content(""))

    expect { doc.set_document_info(creater: "A") }.to raise_error(ArgumentError)
  end
end
