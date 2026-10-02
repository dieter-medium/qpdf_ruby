# frozen_string_literal: true

# The leak the guard rules in ext/qpdf_ruby/ruby_guard.hpp exist to prevent: a Ruby raise that
# longjmps over a live C++ object skips its destructor. LeakSanitizer (bin/asan-rspec.sh) cannot see
# this kind - stale pointers left in dead stack memory make the lost blocks look reachable - so this
# spec watches the process instead: each error path runs many times with a large argument, and a
# skipped destructor would grow RSS by far more than the margin (20,000 x 4 KB = 80 MB per path;
# measured 2026-10-02: 80-160 MB on d472736, which had such leaks, at most 5 MB of noise since).
RSpec.describe QpdfRuby::Document, :memory do
  let(:fixture) { File.expand_path("../fixtures/chromium/chromium_print_cases.pdf", __dir__) }
  let(:payload) { "x" * 4096 }

  def iterations = 20_000

  def margin_kb = 20 * 1024

  def rss_kb = File.read("/proc/self/status")[/VmRSS:\s+(\d+)/, 1].to_i

  # RSS growth over `iterations` calls of the block, after as many calls to warm up - until then
  # the allocator and Ruby's heap still grow for the error messages, a leak keeps growing - and a
  # full GC on both sides.
  def growth_kb(&)
    iterations.times(&)
    GC.start
    before = rss_kb
    iterations.times(&)
    GC.start
    rss_kb - before
  end

  def ignoring(error)
    yield
  rescue error
    nil
  end

  # QPDF reports its recovery attempts on the file descriptor itself, three lines per broken file.
  def quietly
    saved = $stderr.dup
    $stderr.reopen(File::NULL, "w")
    yield
  ensure
    $stderr.reopen(saved)
    saved.close
  end

  before { skip "needs /proc (Linux)" unless File.exist?("/proc/self/status") }

  it "frees the file name when writing fails" do
    doc = described_class.new(fixture)
    path = "/nonexistent/#{payload}.pdf"

    expect(growth_kb { ignoring(QpdfRuby::Error) { doc.write(path) } }).to be < margin_kb
  end

  it "frees the bytes when reading from memory fails" do
    bytes = "%PDF-1.7\n#{payload}"
    growth = quietly { growth_kb { ignoring(QpdfRuby::Error) { described_class.from_memory(bytes) } } }

    expect(growth).to be < margin_kb
  end

  it "frees the file name when opening fails" do
    path = "/nonexistent/#{payload}.pdf"

    expect(growth_kb { ignoring(QpdfRuby::Error) { described_class.new(path) } }).to be < margin_kb
  end

  it "frees the link texts when a later argument is rejected" do
    doc = described_class.new(fixture)
    texts = { payload => payload }

    expect(growth_kb { ignoring(TypeError) { doc.apply_pdfua_fixes(link_texts: texts, title: 42) } }).to be < margin_kb
  end
end
