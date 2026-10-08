# frozen_string_literal: true

RSpec.describe QpdfRuby::Document, "#set_document_info" do
  def reopened(doc) = described_class.from_memory(doc.to_memory)

  def xmp_values(doc, xpath)
    xmp = Nokogiri::XML(doc.metadata.sub(/\A<\?xpacket[^>]*\?>/, ""), &:strict)
    xmp.xpath(xpath, "xmp" => "http://ns.adobe.com/xap/1.0/", "pdf" => "http://ns.adobe.com/pdf/1.3/",
                     "rdf" => "http://www.w3.org/1999/02/22-rdf-syntax-ns#", "x" => "adobe:ns:meta/").map(&:text)
  end

  def producers(doc)
    doc = reopened(doc)
    [doc.document_info["Producer"], xmp_values(doc, "/x:xmpmeta/rdf:RDF/rdf:Description/pdf:Producer | " \
                                                    "/x:xmpmeta/rdf:RDF/rdf:Description/@pdf:Producer")]
  end

  def with_xmp(body, attributes: "") = described_class.from_memory(EdgeCasePdfs.with_xmp(EdgeCasePdfs.xmp_packet(body, attributes: attributes)))

  let(:creator) { "Formed Success CV (https://www.example.com/?a=1&b=2)" }
  let(:producer) { "Formed Success CV, Beispiel GmbH" }
  let(:pdf_ns) { "http://ns.adobe.com/pdf/1.3/" }

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
      expect(doc.set_document_info(creator: creator)).to eq(changed: true, xmp_error: nil)
    end

    it "changes nothing the second time" do
      doc.set_document_info(creator: creator, producer: producer)

      expect(doc.set_document_info(creator: creator, producer: producer)).to eq(changed: false, xmp_error: nil)
    end
  end

  context "with a value not given" do
    let(:doc) { described_class.from_memory(EdgeCasePdfs.with_page_content("")) }

    it "leaves that entry alone" do
      doc.set_document_info(producer: producer)

      expect(reopened(doc).document_info.key?("Creator")).to be(false)
    end

    it "treats an empty value as not given" do
      expect(doc.set_document_info(creator: "", producer: nil)).to eq(changed: false, xmp_error: nil)
    end
  end

  context "with an existing XMP packet" do
    let(:doc) { with_xmp("<pdfuaid:part>1</pdfuaid:part>") }

    it "adds the creator tool to it" do
      doc.set_document_info(creator: creator)

      expect(xmp_values(reopened(doc), "//xmp:CreatorTool")).to eq([creator])
    end

    it "keeps what it held" do
      doc.set_document_info(creator: creator)

      expect(reopened(doc).metadata).to include("<pdfuaid:part>1</pdfuaid:part>")
    end
  end

  context "with XMP that names another producer" do
    it "replaces an element-form producer, so XMP and the information dictionary agree" do
      doc = with_xmp("<pdf:Producer xmlns:pdf=\"#{pdf_ns}\">Skia</pdf:Producer>")
      doc.set_document_info(producer: producer)

      expect(producers(doc)).to eq([producer, [producer]])
    end

    it "replaces an attribute-form producer" do
      doc = with_xmp("", attributes: " xmlns:pdf=\"#{pdf_ns}\" pdf:Producer=\"Skia\"")
      doc.set_document_info(producer: producer)

      expect(producers(doc)).to eq([producer, [producer]])
    end

    it "replaces every copy of it" do
      doc = with_xmp("<pdf:Producer xmlns:pdf=\"#{pdf_ns}\">Skia</pdf:Producer><pdf:Producer xmlns:pdf=\"#{pdf_ns}\">x</pdf:Producer>")
      doc.set_document_info(producer: producer)

      expect(producers(doc)).to eq([producer, [producer]])
    end

    it "leaves the other properties of that Description in place" do
      doc = with_xmp("<pdf:Producer xmlns:pdf=\"#{pdf_ns}\">Skia</pdf:Producer><pdfuaid:part>1</pdfuaid:part>")
      doc.set_document_info(producer: producer)

      expect(reopened(doc).metadata).to include("<pdfuaid:part>1</pdfuaid:part>")
    end
  end

  context "with XMP that already names this producer" do
    let(:doc) { with_xmp("<pdf:Producer xmlns:pdf=\"#{pdf_ns}\">Beispiel &amp; Co</pdf:Producer>") }

    it "leaves the packet as it is" do
      before = doc.metadata
      doc.set_document_info(producer: "Beispiel & Co")

      expect(doc.metadata).to eq(before)
    end
  end

  context "with a pdf:Producer element that is not a property of the document" do
    let(:doc) { with_xmp("<xmp:Note xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"><pdf:Producer xmlns:pdf=\"#{pdf_ns}\">Skia</pdf:Producer></xmp:Note>") }

    it "still writes the document's producer" do
      doc.set_document_info(producer: producer)

      expect(producers(doc)).to eq([producer, [producer]])
    end

    it "leaves the nested element alone" do
      doc.set_document_info(producer: producer)

      expect(reopened(doc).metadata).to include("<pdf:Producer xmlns:pdf=\"#{pdf_ns}\">Skia</pdf:Producer></xmp:Note>")
    end
  end

  context "with XMP that is not well-formed" do
    let(:xmp) { "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"></x:xmpmeta>" }
    let(:doc) { described_class.from_memory(EdgeCasePdfs.with_xmp(xmp)) }

    it "leaves the packet as it was" do
      doc.set_document_info(creator: creator)

      expect(doc.metadata).to eq(xmp)
    end

    it "says why it did not write the XMP" do
      expect(doc.set_document_info(creator: creator)[:xmp_error]).to include("not well-formed")
    end

    it "still sets the information dictionary" do
      doc.set_document_info(creator: creator)

      expect(reopened(doc).document_info["Creator"]).to eq(creator)
    end
  end

  context "with a value XML cannot hold" do
    let(:doc) { described_class.from_memory(EdgeCasePdfs.with_page_content("")) }

    it "rejects a NUL" do
      expect { doc.set_document_info(producer: "A\u0000B") }.to raise_error(ArgumentError, /producer must be UTF-8 text/)
    end

    it "rejects a control character" do
      expect { doc.set_document_info(creator: "A\u0007B") }.to raise_error(ArgumentError, /creator/)
    end

    it "rejects bytes that are not UTF-8" do
      expect { doc.set_document_info(creator: "\xFF".b) }.to raise_error(ArgumentError, /creator/)
    end

    it "accepts tabs, line breaks and characters beyond the BMP" do
      expect(doc.set_document_info(creator: "A\tB\n😀")[:changed]).to be(true)
    end

    it "changes nothing when it rejects a value" do
      doc.set_document_info(creator: "A\u0000") rescue ArgumentError # rubocop:disable Style/RescueModifier

      expect(reopened(doc).document_info.key?("Creator")).to be(false)
    end
  end

  it "rejects an unknown keyword" do
    doc = described_class.from_memory(EdgeCasePdfs.with_page_content(""))

    expect { doc.set_document_info(creater: "A") }.to raise_error(ArgumentError)
  end
end
