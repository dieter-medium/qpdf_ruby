# frozen_string_literal: true

RSpec.describe QpdfRuby do
  def fixture_file(filename)
    File.expand_path("./fixtures/#{filename}", __dir__)
  end

  let(:tmp_file) { File.expand_path("../tmp/example_accessibility_test.pdf", __dir__) }
  let(:tmp_file_from_memory) { File.expand_path("../tmp/example_accessibility_test_memory.pdf", __dir__) }
  let(:expected_structure) { File.read(fixture_file("expected_structure_after_patch.xml")) }

  before do
    FileUtils.mkdir_p(File.dirname(tmp_file))
  end

  it "has a version number" do
    expect(QpdfRuby::VERSION).not_to be_nil
  end

  it "patches the StructTreeRoot" do
    doc = QpdfRuby::Document.new(fixture_file("example_accessibility.pdf"))
    doc.mark_paths_as_artifacts
    doc.ensure_bbox
    doc.write tmp_file

    doc = QpdfRuby::Document.new tmp_file
    actual_structure = doc.show_structure

    actual_xml = Nokogiri::XML(actual_structure, &:noblanks)
    expected_xml = Nokogiri::XML(expected_structure, &:noblanks)

    expect(actual_xml.to_s).to eq(expected_xml.to_s)
  end

  it "round-trips a PDF in RAM" do
    in_buf = File.binread(fixture_file("example_accessibility.pdf"))

    doc = QpdfRuby::Document.from_memory(in_buf, "")
    doc.mark_paths_as_artifacts
    doc.ensure_bbox

    out_buf = doc.to_memory

    File.binwrite(tmp_file_from_memory, out_buf)

    doc = QpdfRuby::Document.new tmp_file

    actual_structure = doc.show_structure

    actual_xml = Nokogiri::XML(actual_structure, &:noblanks)
    expected_xml = Nokogiri::XML(expected_structure, &:noblanks)

    expect(actual_xml.to_s).to eq(expected_xml.to_s)
  end
end
