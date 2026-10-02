# QpdfRuby

> **Make Chromium's tagged PDFs PDF/UA-1 conformant.**

QpdfRuby is a small Ruby wrapper around the [QPDF](https://qpdf.sourceforge.net/) C++ library,
made for PDFs printed by Chromium (`Page.printToPDF` with `generateTaggedPDF`) and checked with
veraPDF or PAC 2024. Chromium tags the text of a page but leaves gaps that PDF/UA-1 rejects; the
gem closes them without touching the content that is tagged:

| PDF/UA-1 rule (veraPDF) | What Chromium does | What QpdfRuby does | Ruby API |
| --- | --- | --- | --- |
| 7.1-3 untagged content | backgrounds, borders, bars, `aria-hidden` and SVG decorations end up outside any marked content; it never writes `/Artifact` | wraps every painting operation outside marked content in `/Artifact BMC … EMC` (not a Form XObject that carries tagged content) | `doc.mark_untagged_content_as_artifacts` |
| 7.18.1-2, 7.18.5-2 link descriptions | no `/Contents` on Link annotations | your text per URI, else the structure element's `/Alt`/`/ActualText`, else the URI (internal links: your text for `"#<destination>"`, else "Page N" - named destinations from `/Dests`, string destinations from the `/Names` tree) | `doc.describe_links(texts = {})` |
| 7.2-20 list items | `LI` holds `Lbl` and the content directly, never `LBody` | moves the content into an `LBody`, ParentTree entries updated (a Form XObject's MCIDs in its own entry) | `doc.wrap_list_bodies` |
| 7.1-5 non-standard types | PDF 2.0 types (`Strong`, `Em`, `Aside`, …) without a RoleMap | maps them to PDF 1.7 types in the RoleMap (table below) | `doc.map_nonstandard_roles` |
| 7.3-1 figure alternative | an HTML `<figure>` becomes a `Figure` without alt text around the image's own `Figure` | retags that grouping `Figure` as `Div` | `doc.retag_grouping_figures` |
| 7.1-8 metadata | no XMP `pdfuaid` identification | adds `pdfuaid:part 1` and `dc:title`, each only if missing (extends existing XMP, never replaces a title), sets `DisplayDocTitle` and `Marked` - the identification only for a file with a structure tree | `doc.add_pdfua_identification(title: nil)` |

`doc.apply_pdfua_fixes(link_texts: {}, title: nil)` runs all of them and returns a report.
Each step only adds what is missing, so running it twice changes nothing (the report's
`identified` is true only when the identification step changed something). When the file cannot
be identified - it has no structure tree, or its existing XMP has no RDF element the gem can find -
the other steps still run and `unidentified_reason` says why; it is `nil` otherwise. Figures that
have no alternative text at all cannot be fixed by a tool - `doc.figures_without_alt` counts them.

The RoleMap entries `map_nonstandard_roles` adds, each only for a type the file uses and does not
map yet. Some are lossy - an assistive technology reads `Title` as a paragraph, `Aside` as a
section:

| PDF 2.0 type | mapped to |
| --- | --- |
| `Aside` | `Sect` |
| `DocumentFragment` | `Part` |
| `Em`, `Strong`, `Sub` | `Span` |
| `FENote` | `Note` |
| `Title` | `P` |

Untagged content is looked for in page content and the Form XObjects it draws - not in annotation
appearance streams, tiling patterns or Type 3 glyph procedures, which Chromium does not emit.

Also:

| Feature | Ruby API |
| --- | --- |
| Count untagged content (dry run, per kind) | `doc.untagged_content` |
| Add a layout `/BBox` to every `/Figure`¹ | `doc.ensure_bbox` |
| Dump the structure tree as XML | `doc.show_structure` |
| Inspect links, XMP, RoleMap | `doc.links`, `doc.metadata`, `doc.role_map` |
| Count ParentTree entries that do not name their content's structure element (0 = consistent) | `doc.parent_tree_mismatches` |
| Encrypt | `doc.encrypt(user_pw:, owner_pw:, …)` |
| Read/write files or memory | `Document.new(path, password = nil)`, `Document.from_memory(bytes, password = nil)`, `#write(path)`, `#to_memory` |

_¹The gem parses each page's content stream, and the content of the Form XObjects it draws, under
the full transformation matrix and unites what each piece of marked content paints - images, Form
XObjects, paths - per page, content stream and MCID (a marked-content reference's `/Stm` names the
form whose MCIDs it means). Text adds nothing (its extent needs font metrics), and content outside
the crop box is ignored; a Figure with no other content gets the crop box._

`#mark_paths_as_artifacts` is deprecated: it now does what `#mark_untagged_content_as_artifacts`
does. Its old behaviour - wrapping rectangles anywhere, also inside tagged content - fixed few
Chromium decorations and broke rules 7.1-1/7.1-2.

Every error raises `QpdfRuby::Error` (a `RuntimeError`).

Checked against veraPDF 1.30.2 on Chromium 154 output (`spec/fixtures/chromium/`): both
fixtures are PDF/UA-1 compliant after `apply_pdfua_fixes`. A machine check covers the machine
checkable part only - alternative texts, headings and reading order still need a person.

---

## Installation

### Requirements

* **Ruby** \>= 3.3 (tested with 3.3, 3.4 and 4.0)
* **QPDF** \>= 11.9 (headers & libs; tested with 11.9.1 and 12.2)

### macOS
```bash
brew install qpdf
bundle config set --local build.qpdf_ruby "--with-qpdf-dir=$(brew --prefix qpdf)"
```  

### Debian/Ubuntu
```bash
# Debian 13 (trixie) and newer ship QPDF 12
sudo apt-get update && sudo apt-get install -y libqpdf-dev qpdf
```
If `apt` cannot provide QPDF ≥ 11.9, compile it yourself or use Debian trixie (see the
[Dockerfile](./docker/Dockerfile)).

### Add the gem
```bash
bundle add qpdf_ruby
# …or without bundler:
# gem install qpdf_ruby -- --with-qpdf-include=/usr/local/include/qpdf --with-qpdf-lib=/usr/local/lib
```

---

## Quick Start
```ruby
require "qpdf_ruby"

pdf = QpdfRuby::Document.new("chromium-output.pdf")

report = pdf.apply_pdfua_fixes(
  link_texts: { "mailto:jana@example.com" => "E-mail Jana" }, # optional, per URI or "#destination"
  title: "Curriculum vitae - Jana Example"                    # optional, else the PDF's /Title
)
# => { artifacts: { paths: 75, texts: 101, …, total: 176 }, links: 2, list_bodies: 34, roles: 0,
#      figure_groups: 0, figures_without_alt: 0, identified: true, unidentified_reason: nil }

pdf.ensure_bbox                     # layout BBoxes for figures (PAC 2024 asks for them)
pdf.write("accessible.pdf")         # or pdf.to_memory
```

---

## Development
```bash
git clone https://github.com/dieter-medium/qpdf_ruby.git
cd qpdf_ruby
bin/setup        # install gem + test deps
autotest         # guard & RSpec
```
* Bump **version.rb** → `bundle exec rake release` to push a new gem. It first runs
  `rake release_credentials_check`, which shows where the push credential comes from: a set
  `GEM_HOST_API_KEY` wins (as in RubyGems), else `gem.push_key` in the credentials file RubyGems uses.
* `bundle exec rspec` runs the specs; the veraPDF examples run only where `verapdf` is on the
  `PATH` (`bin/install-verapdf.sh [DIR]` installs the pinned CLI; needs Java). They are skipped
  otherwise - `REQUIRE_VERAPDF=1`, set in CI, makes them fail instead.

### Testing with local QPDF builds
If you tinker with QPDF itself, point Bundler to your custom prefix:
```bash
bundle config set --local build.qpdf_ruby "--with-qpdf-include=$HOME/opt/qpdf/include --with-qpdf-lib=$HOME/opt/qpdf/lib"
```

---

## Roadmap

  TBD

---

## Contributing
Bug reports & pull requests are welcome at
<https://github.com/dieter-medium/qpdf_ruby>.

### Code Style
* C++ 17, clang‑format enforced
* Ruby 3.3, rubocop default rules

---

## License

[MIT](https://opensource.org/licenses/MIT) – see `LICENSE.txt` for full
text.
