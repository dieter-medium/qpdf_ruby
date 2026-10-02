# frozen_string_literal: true

# Synthetic PDFs for the second review of PR #3 (spec/acceptance/pdfua_second_review_spec.rb), built
# with MinimalPdf.
module UnusualInputPdfs
  module_function

  # One untagged page drawing `content`, plus a Form XObject /Fm whose content is `form_content`.
  def with_form(form_content, content: "/Fm Do\n")
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /XObject << /Fm 5 0 R >> >> " \
      "/Contents 4 0 R >>",
      ["<< >>", content],
      ["<< /Type /XObject /Subtype /Form /BBox [0 0 200 200] >>", form_content]
    ])
  end

  # A Figure whose only content (MCID 0) is drawn at 300,300 - outside the 200 x 200 page.
  def with_figure_outside_crop_box
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 5 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /StructParents 0 /Contents 4 0 R >>",
      ["<< >>", "/Figure << /MCID 0 >> BDC 300 300 20 20 re f EMC\n"],
      "<< /Type /StructTreeRoot /K [6 0 R] /ParentTree << /Nums [0 [6 0 R]] >> /ParentTreeNextKey 1 >>",
      "<< /Type /StructElem /S /Figure /P 5 0 R /Pg 3 0 R /Alt (Off page) /K 0 >>"
    ])
  end

  # A list item on page 1 whose content (MCID 0) sits on page 2, referenced by an MCR with /Pg.
  def with_list_item_on_two_pages
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R >>",
      "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /StructParents 0 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /StructParents 1 /Contents 5 0 R >>",
      ["<< >>", "/LI << /MCID 0 >> BDC 10 10 20 20 re f EMC\n"],
      "<< /Type /StructTreeRoot /K [7 0 R] /ParentTree << /Nums [0 [] 1 [8 0 R]] >> /ParentTreeNextKey 2 >>",
      "<< /Type /StructElem /S /L /P 6 0 R /Pg 3 0 R /K [8 0 R] >>",
      "<< /Type /StructElem /S /LI /P 7 0 R /Pg 3 0 R /K [<< /Type /MCR /MCID 0 /Pg 4 0 R >>] >>"
    ])
  end

  # A list item whose object reference points at something that is not a dictionary.
  def with_broken_object_reference
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 4 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
      "<< /Type /StructTreeRoot /K [5 0 R] /ParentTree << /Nums [] >> >>",
      "<< /Type /StructElem /S /L /P 4 0 R /K [6 0 R] >>",
      "<< /Type /StructElem /S /LI /P 5 0 R /Pg 3 0 R /K [<< /Type /OBJR /Obj 7 0 R >>] >>",
      "42"
    ])
  end

  # A Figure (MCID 0) whose page content is `content`, wrapped in its marked-content sequence;
  # `resources` is the page's /Resources dictionary, `attributes` extra entries of the Figure
  # (e.g. "/A << ... >>"), and `parents` the types of the elements between the root and the Figure,
  # outermost first (objects 6, 7, ...; the Figure comes last).
  def with_figure_drawing(content, resources: "<< >>", attributes: "", parents: [])
    figure_number = 6 + parents.size
    wrappers = parents.each_with_index.map do |type, i|
      "<< /Type /StructElem /S /#{type} /P #{i.zero? ? 5 : 5 + i} 0 R /Pg 3 0 R /K [#{7 + i} 0 R] >>"
    end
    figure = "<< /Type /StructElem /S /Figure /P #{figure_number - 1} 0 R /Pg 3 0 R /Alt (Drawing) /K 0 #{attributes} >>"
    MinimalPdf.build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 5 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /StructParents 0 /Resources #{resources} " \
      "/Contents 4 0 R >>",
      ["<< >>", "/Figure << /MCID 0 >> BDC #{content} EMC\n"],
      "<< /Type /StructTreeRoot /K [6 0 R] /ParentTree << /Nums [0 [#{figure_number} 0 R]] >> /ParentTreeNextKey 1 >>",
      *wrappers, figure
    ])
  end
end
