# QpdfRuby

> **Patch & polish PDFs so that PAC 2024 finally turns green.**

QpdfRuby is a very small Ruby wrapper around the battle‑tested
[QPDF \>= 12](https://qpdf.sourceforge.net/) C++ library.  Right now the
library focuses on only **three specialised tasks** that are needed when
PDFs are printed from Chromium‑based browsers and subsequently audited
with the PAC 2024 accessibility checker:

1. **Export the structure tree as XML** – handy for debugging.
2. **Mark vector path objects as `/Artifact`** so that decorative lines,
   boxes, &c. are ignored by assistive technologies.
3. **Add missing `/BBox` entries to every `/Figure` element** (derived
   from the page’s graphic operators) so that screen readers know the
   physical extent of each image.

Together these tweaks eliminate the most common complaints PAC 2024 has
about browser‑generated PDFs.

---

## Features in Detail

| Feature                                        | Ruby API                                 |
| ---------------------------------------------- | ---------------------------------------- |
| Dump structure tree as XML                     | `doc.show_structure`                     |
| Mark path objects ( `re … S/s/f/F/B/b` )       | `doc.mark_paths_as_artifacts`            |
| Ensure `/Figure` elements have a layout BBox¹  | `doc.ensure_bbox`                        |

_¹Internally the gem parses each page’s content stream, maps image
`/MCID`s to their transformation matrix, computes the bounding box
(courtesy of a little linear algebra) and finally writes the result into
the structure tree._

---

## Installation

### Requirements

* **Ruby** \>= 3.1
* **QPDF** \>= 12.0.0 (headers & libs)

### macOS
```bash
brew install qpdf
bundle config set --local build.qpdf_ruby "--with-qpdf-dir=$(brew --prefix qpdf)"
```  

### Debian/Ubuntu
```bash
# on Debian 11/Ubuntu 20.04 you may need newer packages from testing
sudo apt-get update && sudo apt-get install -y libqpdf-dev qpdf
```
If `apt` cannot provide QPDF ≥ 12 you can compile it yourself or pull the
package from *testing/unstable* – see the [Dockerfile](./docker/Dockerfile) for a working
`apt preferences` snippet.

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

pdf = QpdfRuby::Document.new("input.pdf")

# 1. tag decorative paths
pdf.mark_paths_as_artifacts

# 2. add BBox to every <Figure>
pdf.ensure_bbox

# 3. introspect structure tree (optional)
File.write("structure.xml", pdf.show_structure)

# 4. save 🎉
pdf.write("fixed.pdf")
```

Run PAC 2024 on `fixed.pdf` – it should report far fewer (or zero!)
errors compared to the original browser output.

---

## Development
```bash
git clone https://github.com/dieter-medium/qpdf_ruby.git
cd qpdf_ruby
bin/setup        # install gem + test deps
autotest         # guard & RSpec
```
* Bump **version.rb** → `bundle exec rake release` to push a new gem.

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
* Ruby 3.2, rubocop default rules

---

## License

[MIT](https://opensource.org/licenses/MIT) – see `LICENSE.txt` for full
text.
