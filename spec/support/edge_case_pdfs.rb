# frozen_string_literal: true

require "zlib"

# Synthetic PDFs for the edge cases the PR #3 review found (spec/acceptance/pdfua_review_findings_spec.rb),
# built with MinimalPdf.
module EdgeCasePdfs
  module_function

  # One page whose content stream is `content`; no structure tree.
  def with_page_content(content)
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>",
      ["<< >>", content]
    ])
  end

  # A list of two items whose content both uses MCID 0: LI A's paragraph in the page's content,
  # LI B's marked-content reference in a Form XObject (its own /StructParents, /Stm in the MCR).
  def with_list_items_sharing_mcid
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R /MarkInfo << /Marked true >> >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /StructParents 0 " \
      "/Resources << /XObject << /Fm 5 0 R >> >> /Contents 4 0 R >>",
      ["<< >>", "/P << /MCID 0 >> BDC 0 0 1 rg 10 10 20 20 re f EMC\n/Fm Do\n"],
      ["<< /Type /XObject /Subtype /Form /BBox [0 0 200 200] /StructParents 1 >>",
       "/LI << /MCID 0 >> BDC 1 0 0 rg 100 100 20 20 re f EMC\n"],
      "<< /Type /StructTreeRoot /K [7 0 R] /ParentTree << /Nums [0 [9 0 R] 1 [10 0 R]] >> /ParentTreeNextKey 2 >>",
      "<< /Type /StructElem /S /L /P 6 0 R /Pg 3 0 R /K [8 0 R 10 0 R] >>",
      "<< /Type /StructElem /S /LI /P 7 0 R /Pg 3 0 R /K [9 0 R] >>",
      "<< /Type /StructElem /S /P /P 8 0 R /Pg 3 0 R /K 0 >>",
      "<< /Type /StructElem /S /LI /P 7 0 R /Pg 3 0 R /K [<< /Type /MCR /MCID 0 /Pg 3 0 R /Stm 5 0 R >>] >>"
    ])
  end

  # A tagged one-page file (an empty structure tree) whose catalog carries `xmp` as its metadata.
  def with_xmp(xmp)
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R /StructTreeRoot 5 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
      ["<< /Type /Metadata /Subtype /XML >>", xmp],
      "<< /Type /StructTreeRoot /K [] >>"
    ])
  end

  # An XMP packet whose RDF namespace is bound to `rdf` and whose only Description holds `body`
  # (`attributes` go on that Description).
  def xmp_packet(body = "", attributes: "", rdf: "rdf")
    "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n" \
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><#{rdf}:RDF xmlns:#{rdf}=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n" \
      "<#{rdf}:Description #{rdf}:about=\"\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\"#{attributes}>" \
      "#{body}</#{rdf}:Description>\n</#{rdf}:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>"
  end

  # Existing XMP metadata that already identifies the file as PDF/UA-1 (`part`, element or
  # attribute form) but has no dc:title.
  def with_pdfua_xmp_without_title(part = "<pdfuaid:part>1</pdfuaid:part>")
    attribute = part.start_with?("<") ? "" : " #{part}"
    with_xmp(xmp_packet(part.start_with?("<") ? part : "", attributes: attribute))
  end

  # Two pages; page 1 links to the string destination (kapitel) - page 2 - held in the catalog's
  # /Names /Dests name tree, flat or split into /Kids.
  def with_named_string_destination(kids: false)
    tree = kids ? "6 0 R" : "<< /Names [(kapitel) [4 0 R /Fit]] >>"
    objects = [
      "<< /Type /Catalog /Pages 2 0 R /Names << /Dests #{tree} >> >>",
      "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Annots [5 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
      "<< /Type /Annot /Subtype /Link /Rect [10 10 50 30] /A << /S /GoTo /D (kapitel) >> >>"
    ]
    if kids
      objects << "<< /Kids [7 0 R] >>"
      objects << "<< /Limits [(a) (z)] /Names [(kapitel) << /D [4 0 R /Fit] >>] >>"
    end
    MinimalPdf.build(objects)
  end

  # A paragraph with MCID 0 in the page's content at [10 10 30 30], and a Figure with MCID 0 in a
  # Form XObject (BBox [0 0 50 50], /Matrix scaling by 2) whose image-like square [0 0 10 10] is
  # drawn at 100,100 - the Figure covers [100 100 120 120].
  def with_figure_in_form_sharing_mcid
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R /MarkInfo << /Marked true >> >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /StructParents 0 " \
      "/Resources << /XObject << /Fm 5 0 R >> >> /Contents 4 0 R >>",
      ["<< >>", "/P << /MCID 0 >> BDC 0 0 1 rg 10 10 20 20 re f EMC\nq 1 0 0 1 100 100 cm /Fm Do Q\n"],
      ["<< /Type /XObject /Subtype /Form /BBox [0 0 50 50] /Matrix [2 0 0 2 0 0] /StructParents 1 >>",
       "/Figure << /MCID 0 >> BDC 1 0 0 rg 0 0 10 10 re f EMC\n"],
      "<< /Type /StructTreeRoot /K [7 0 R 8 0 R] /ParentTree << /Nums [0 [7 0 R] 1 [8 0 R]] >> " \
      "/ParentTreeNextKey 2 >>",
      "<< /Type /StructElem /S /P /P 6 0 R /Pg 3 0 R /K 0 >>",
      "<< /Type /StructElem /S /Figure /P 6 0 R /Pg 3 0 R /Alt (Logo) " \
      "/K << /Type /MCR /MCID 0 /Pg 3 0 R /Stm 5 0 R >> >>"
    ])
  end

  # The decompressed content streams of a written PDF, joined - QPDFWriter compresses them.
  def content_streams(bytes)
    bytes.b.scan(/stream\r?\n(.*?)endstream/m).map do |(data)|
      Zlib::Inflate.inflate(data)
    rescue Zlib::Error
      data
    end.join("\n")
  end
end
