# frozen_string_literal: true

# ensure_bbox on stroked paths: the BBox covers the painted stroke - line width, caps, joins and the
# CTM - not just the path's centre line. Expected boxes are worked out by hand in default user space.
RSpec.describe "Figure bounds of stroked paths" do
  def figure_bbox(content, **options)
    doc = QpdfRuby::Document.from_memory(UnusualInputPdfs.with_figure_drawing(content, **options))
    doc.ensure_bbox
    structure = QpdfRuby::Document.from_memory(doc.to_memory).show_structure
    structure[/<Figure [^>]*BBox="\[([^\]]*)\]"/, 1].split(",").map(&:to_f)
  end

  def be_box(*expected) = match(expected.map { |value| be_within(0.001).of(value) })

  it "covers a horizontal line 20 wide, not just its centre line" do
    expect(figure_bbox("20 w 10 100 m 110 100 l S")).to be_box(10, 90, 110, 110)
  end

  it "adds projecting square caps" do
    expect(figure_bbox("20 w 2 J 10 100 m 110 100 l S")).to be_box(0, 90, 120, 110)
  end

  it "adds round caps as ellipses under a scaling CTM" do
    expect(figure_bbox("2 0 0 1 0 0 cm 20 w 1 J 10 100 m 60 100 l S")).to be_box(0, 90, 140, 110)
  end

  it "adds the tip of a miter join" do
    expect(figure_bbox("10 w 50 50 m 100 150 l 150 50 l S")).to be_box(45.528, 47.764, 154.472, 161.180)
  end

  it "puts the miter tip on the outside of a left turn too" do
    expect(figure_bbox("10 w 150 50 m 100 150 l 50 50 l S")).to be_box(45.528, 47.764, 154.472, 161.180)
  end

  it "bevels a join beyond the miter limit" do
    expect(figure_bbox("10 w 1.5 M 50 50 m 100 150 l 150 50 l S")).to be_box(45.528, 47.764, 154.472, 152.236)
  end

  it "takes the line width from an ExtGState" do
    expect(figure_bbox("/GS0 gs 10 100 m 110 100 l S", resources: "<< /ExtGState << /GS0 << /LW 20 >> >> >>"))
      .to be_box(10, 90, 110, 110)
  end

  it "covers a stroked rectangle's mitered corners" do
    expect(figure_bbox("10 w 50 50 100 100 re S")).to be_box(45, 45, 155, 155)
  end

  it "restores the line width with Q" do
    expect(figure_bbox("q 20 w Q 10 100 m 110 100 l S")).to be_box(10, 99.5, 110, 100.5)
  end

  it "keeps a filled path to its outline" do
    expect(figure_bbox("20 w 10 100 m 110 100 l 110 150 l f")).to be_box(10, 100, 110, 150)
  end
end
