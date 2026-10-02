# frozen_string_literal: true

# ensure_bbox also gives a Figure in a block context an explicit Placement /Block - without it PAC
# 2024 warns "Possibly inappropriate use of a Figure structure element" (checked 2026-10-02 against
# the Modern CV: the warning went away with Placement /Block, not with /Inline or no attribute).
RSpec.describe "Figure placement" do
  def placed(**options)
    doc = QpdfRuby::Document.from_memory(UnusualInputPdfs.with_figure_drawing("0 0 10 10 re f", **options))
    doc.ensure_bbox
    doc.to_memory
  end

  it "places a Figure in a block context as a block" do
    expect(placed(parents: %w[Div])).to match(%r{/Placement\s*/Block})
  end

  it "places a Figure directly below the root as a block" do
    expect(placed).to match(%r{/Placement\s*/Block})
  end

  it "leaves a Figure inside a paragraph inline" do
    expect(placed(parents: %w[P])).not_to include("/Placement")
  end

  it "looks through NonStruct wrappers to the paragraph" do
    expect(placed(parents: %w[P NonStruct])).not_to include("/Placement")
  end

  it "adds the placement to a layout attribute that already has a BBox" do
    expect(placed(attributes: "/A << /O /Layout /BBox [1 2 3 4] >>"))
      .to match(%r{/BBox\s*\[\s*1\s+2\s+3\s+4\s*\].*/Placement\s*/Block|/Placement\s*/Block.*/BBox\s*\[\s*1\s+2\s+3\s+4\s*\]}m)
  end

  it "keeps a placement that is already set" do
    expect(placed(attributes: "/A << /O /Layout /Placement /Inline >>")).not_to include("/Block")
  end
end
