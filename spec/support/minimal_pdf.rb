# frozen_string_literal: true

# Writes small, valid PDFs for cases no real fixture covers. Objects are given as strings (dicts)
# or [dict, stream] pairs, numbered from 1 in order; the xref offsets are computed, so QPDF reads the
# file without recovering anything.
module MinimalPdf
  module_function

  def build(objects, root: 1)
    body = +"%PDF-1.7\n%\xE2\xE3\xCF\xD3\n".b
    offsets = objects.each_with_index.map { |object, index| append_object(body, index + 1, object) }
    append_trailer(body, offsets, root)
  end

  def append_object(body, number, object)
    offset = body.bytesize
    body << "#{number} 0 obj\n".b << serialize(object) << "\nendobj\n".b
    offset
  end

  def append_trailer(body, offsets, root)
    xref = body.bytesize
    body << "xref\n0 #{offsets.size + 1}\n0000000000 65535 f \n".b
    offsets.each { |offset| body << format("%010d 00000 n \n", offset).b }
    body << "trailer\n<< /Size #{offsets.size + 1} /Root #{root} 0 R >>\nstartxref\n#{xref}\n%%EOF\n".b
  end

  def serialize(object)
    return object.b unless object.is_a?(Array)

    dict, stream = object
    "#{dict.sub(/>>\s*\z/, " /Length #{stream.bytesize} >>")}\nstream\n#{stream}\nendstream".b
  end

  # One page: an untagged rectangle, a Form XObject whose content is tagged (MCID 0 of a P element,
  # referenced through the form's StructParents) and an untagged decorative Form XObject.
  def with_tagged_form_xobject
    build([
      "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R /MarkInfo << /Marked true >> /Lang (en) >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /StructParents 0 " \
      "/Resources << /XObject << /Tagged 5 0 R /Plain 8 0 R >> >> /Contents 4 0 R >>",
      ["<< >>", "0 0 1 rg 10 10 50 50 re f\n/Tagged Do\n/Plain Do\n"],
      ["<< /Type /XObject /Subtype /Form /BBox [0 0 200 200] /StructParents 1 >>",
       "/P << /MCID 0 >> BDC 1 0 0 rg 100 100 20 20 re f EMC\n"],
      "<< /Type /StructTreeRoot /K [7 0 R] /ParentTree << /Nums [0 [] 1 [7 0 R]] >> /ParentTreeNextKey 2 >>",
      "<< /Type /StructElem /S /P /P 6 0 R /Pg 3 0 R /K << /Type /MCR /MCID 0 /Pg 3 0 R /Stm 5 0 R >> >>",
      ["<< /Type /XObject /Subtype /Form /BBox [0 0 200 200] >>", "0 1 0 rg 150 150 10 10 re f\n"]
    ])
  end
end
