# frozen_string_literal: true

# Chromium tags CSS backgrounds and borders as content of the grouping element they belong to - a
# Table's cell backgrounds, an <article>'s card. PAC 2024 rejects content in those places ("Content in
# an inadmissible location", "Invalid use of a TR"); artifact_tagged_decorations turns such shape-only
# content into artifacts. Fixtures: TaggedDecorationPdfs.
RSpec.describe "Decorations tagged as content of a grouping element" do
  def open_pdf(bytes) = QpdfRuby::Document.from_memory(bytes)

  def reopened(doc) = QpdfRuby::Document.from_memory(doc.to_memory)

  def structure(doc) = Nokogiri::XML("<root>#{doc.show_structure}</root>")

  # The MCIDs an element of type `name` holds directly.
  def own_mcids(doc, name)
    structure(doc).at_xpath("//#{name}").xpath("text()").map(&:text).join.scan(/\[MCID: (\d+)\]/).flatten.map(&:to_i)
  end

  context "with a table whose cell backgrounds are tagged as the Table's content" do
    let(:doc) { open_pdf(TaggedDecorationPdfs.table_with_tagged_background) }

    it "reports one decoration" do
      expect(doc.artifact_tagged_decorations).to eq(1)
    end

    it "leaves the Table only its rows" do
      doc.artifact_tagged_decorations

      expect(own_mcids(reopened(doc), "Table")).to be_empty
    end

    it "paints the backgrounds as an artifact" do
      doc.artifact_tagged_decorations

      expect(EdgeCasePdfs.content_streams(doc.to_memory)).to match(%r{/Artifact\s+BMC\s+0\.9 g 10 10 100 40 re f EMC})
    end

    it "keeps the ParentTree consistent" do
      doc.artifact_tagged_decorations

      expect(reopened(doc).parent_tree_mismatches).to eq(0)
    end

    it "leaves no content untagged" do
      doc.artifact_tagged_decorations

      expect(reopened(doc).untagged_content[:total]).to eq(0)
    end

    it "changes nothing the second time" do
      doc.artifact_tagged_decorations

      expect(doc.artifact_tagged_decorations).to eq(0)
    end
  end

  context "with an Art element holding a background and text of its own" do
    let(:doc) { open_pdf(TaggedDecorationPdfs.art_with_background_and_text) }

    it "turns only the background into an artifact" do
      doc.artifact_tagged_decorations

      expect(own_mcids(reopened(doc), "Art")).to eq([1])
    end
  end

  context "with a decoration whose MCID is in a /Properties resource" do
    let(:doc) { open_pdf(TaggedDecorationPdfs.div_with_named_properties) }

    it "finds and removes it" do
      doc.artifact_tagged_decorations

      expect(own_mcids(reopened(doc), "Div")).to be_empty
    end

    it "keeps the ParentTree consistent" do
      doc.artifact_tagged_decorations

      expect(reopened(doc).parent_tree_mismatches).to eq(0)
    end
  end

  context "with a decoration in a Form XObject next to page text of the same MCID" do
    let(:doc) { open_pdf(TaggedDecorationPdfs.div_with_decoration_in_form) }

    it "turns the form's content into an artifact" do
      expect(doc.artifact_tagged_decorations).to eq(1)
    end

    it "leaves the page's MCID 0 to its paragraph" do
      doc.artifact_tagged_decorations

      expect(own_mcids(reopened(doc), "P")).to eq([0])
    end

    it "keeps the ParentTree consistent" do
      doc.artifact_tagged_decorations

      expect(reopened(doc).parent_tree_mismatches).to eq(0)
    end
  end

  context "with an MCID out of range elsewhere in the content" do
    it "still turns the decoration into an artifact" do
      expect(open_pdf(TaggedDecorationPdfs.div_after_huge_mcid).artifact_tagged_decorations).to eq(1)
    end
  end

  context "with shapes drawn around nested tagged content" do
    it "leaves them tagged" do
      expect(open_pdf(TaggedDecorationPdfs.div_around_tagged_content).artifact_tagged_decorations).to eq(0)
    end
  end
end
