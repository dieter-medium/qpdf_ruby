# frozen_string_literal: true

# Synthetic PDFs with decorations tagged as content of a grouping element - what Chromium writes for a
# table's cell backgrounds or a CSS background behind an <article> (spec/acceptance/tagged_decorations_spec.rb).
module TaggedDecorationPdfs
  module_function

  # One page drawing `content`, with `resources`, and the structure `elements` (object 5 is the
  # StructTreeRoot whose /K is `root_kids`; elements are numbered from 6).
  def page_with(content, elements, root_kids:, parent_tree:, resources: "<< >>")
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 5 0 R /MarkInfo << /Marked true >> >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /StructParents 0 /Resources #{resources} " \
      "/Contents 4 0 R >>",
      ["<< >>", content],
      "<< /Type /StructTreeRoot /K [#{root_kids}] /ParentTree << /Nums [#{parent_tree}] >> /ParentTreeNextKey 2 >>",
      *elements
    ])
  end

  # A table whose cell backgrounds are tagged as the Table's own content (MCID 0), before its rows.
  def table_with_tagged_background
    page_with(
      "/Table << /MCID 0 >> BDC 0.9 g 10 10 100 40 re f EMC\n" \
      "/TH << /MCID 1 >> BDC BT 12 40 Td (Year) Tj ET EMC\n/TD << /MCID 2 >> BDC BT 12 15 Td (2026) Tj ET EMC\n",
      ["<< /Type /StructElem /S /Table /P 5 0 R /Pg 3 0 R /K [0 7 0 R 8 0 R] >>",
       "<< /Type /StructElem /S /TR /P 6 0 R /Pg 3 0 R /K [9 0 R] >>",
       "<< /Type /StructElem /S /TR /P 6 0 R /Pg 3 0 R /K [10 0 R] >>",
       "<< /Type /StructElem /S /TH /P 7 0 R /Pg 3 0 R /K 1 >>",
       "<< /Type /StructElem /S /TD /P 8 0 R /Pg 3 0 R /K 2 >>"],
      root_kids: "6 0 R", parent_tree: "0 [6 0 R 9 0 R 10 0 R]"
    )
  end

  # An Art element with a tagged background (MCID 0) and text of its own (MCID 1), then a P.
  def art_with_background_and_text
    page_with(
      "/Art << /MCID 0 >> BDC 1 0 0 rg 0 0 200 200 re f EMC\n/Art << /MCID 1 >> BDC BT 10 150 Td (Hi) Tj ET EMC\n" \
      "/P << /MCID 2 >> BDC BT 10 100 Td (Text) Tj ET EMC\n",
      ["<< /Type /StructElem /S /Art /P 5 0 R /Pg 3 0 R /K [0 1 7 0 R] >>",
       "<< /Type /StructElem /S /P /P 6 0 R /Pg 3 0 R /K 2 >>"],
      root_kids: "6 0 R", parent_tree: "0 [6 0 R 6 0 R 7 0 R]"
    )
  end

  # A Div whose decoration names its MCID through a /Properties resource, not an inline dictionary.
  def div_with_named_properties
    page_with(
      "/Div /MC0 BDC 0 0 10 10 re f EMC\n/P << /MCID 1 >> BDC BT 10 100 Td (Text) Tj ET EMC\n",
      ["<< /Type /StructElem /S /Div /P 5 0 R /Pg 3 0 R /K [0 7 0 R] >>",
       "<< /Type /StructElem /S /P /P 6 0 R /Pg 3 0 R /K 1 >>"],
      root_kids: "6 0 R", parent_tree: "0 [6 0 R 7 0 R]",
      resources: "<< /Properties << /MC0 << /MCID 0 >> >> >>"
    )
  end

  # A Div whose decoration lives in a Form XObject (MCR with /Stm, the form's MCID 0) next to a P
  # whose text is the page's own MCID 0.
  def div_with_decoration_in_form
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R /MarkInfo << /Marked true >> >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /StructParents 0 " \
      "/Resources << /XObject << /Fm 5 0 R >> >> /Contents 4 0 R >>",
      ["<< >>", "/Fm Do\n/P << /MCID 0 >> BDC BT 10 100 Td (Text) Tj ET EMC\n"],
      ["<< /Type /XObject /Subtype /Form /BBox [0 0 200 200] /StructParents 1 >>",
       "/Div << /MCID 0 >> BDC 0 0 1 rg 0 0 50 50 re f EMC\n"],
      "<< /Type /StructTreeRoot /K [7 0 R] /ParentTree << /Nums [0 [8 0 R] 1 [7 0 R]] >> /ParentTreeNextKey 2 >>",
      "<< /Type /StructElem /S /Div /P 6 0 R /Pg 3 0 R /K [<< /Type /MCR /MCID 0 /Pg 3 0 R /Stm 5 0 R >> 8 0 R] >>",
      "<< /Type /StructElem /S /P /P 7 0 R /Pg 3 0 R /K 0 >>"
    ])
  end

  # A Div with a decoration (MCID 0) after a marked-content sequence whose MCID is out of int range.
  def div_after_huge_mcid
    page_with(
      "/Span << /MCID 99999999999 >> BDC 0 0 1 1 re f EMC\n/Div << /MCID 0 >> BDC 0 0 10 10 re f EMC\n",
      ["<< /Type /StructElem /S /Div /P 5 0 R /Pg 3 0 R /K [0] >>"],
      root_kids: "6 0 R", parent_tree: "0 [6 0 R]"
    )
  end

  # A Div whose own content (MCID 0) draws a shape around a tagged paragraph (MCID 1) - not a
  # decoration on its own.
  def div_around_tagged_content
    page_with(
      "/Div << /MCID 0 >> BDC 0 0 10 10 re f /P << /MCID 1 >> BDC BT 10 100 Td (Text) Tj ET EMC EMC\n",
      ["<< /Type /StructElem /S /Div /P 5 0 R /Pg 3 0 R /K [0 7 0 R] >>",
       "<< /Type /StructElem /S /P /P 6 0 R /Pg 3 0 R /K 1 >>"],
      root_kids: "6 0 R", parent_tree: "0 [6 0 R 7 0 R]"
    )
  end
end
