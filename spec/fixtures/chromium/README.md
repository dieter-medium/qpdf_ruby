# Chromium fixtures

Real output of Chromium's print-to-PDF, printed 2026-10-02 by HeadlessChrome 154.0.0.0
(`browserless/chrome`-style remote-chrome, Linux x86_64) through the Chrome DevTools Protocol's
`Page.printToPDF` with `printBackground: true`, `generateTaggedPDF: true` and
`generateDocumentOutline: true` - the options Rails apps using bidi2pdf print with.

- `chromium_print_cases.pdf` - three pages of the cases Chromium tags in ways PDF/UA-1 rejects:
  decorative boxes, `aria-hidden` and `role="presentation"` content, inline SVG, `::before`
  bullets, an element with `opacity` below 1; an HTML `<figure>` with a raster image, its `alt`
  and a `<figcaption>`, an SVG with `aria-label`, links in `ul`/`ol` items and in a paragraph,
  an internal link to a page anchor; `<aside>`, `<strong>`, `<em>` and a table. Before the fixes
  veraPDF 1.30.2 reports 7.1-3, 7.1-5, 7.1-8, 7.2-20, 7.3-1, 7.18.1-2 and 7.18.5-2.
- `modern_cv.pdf` - a three-page CV (synthetic data) from the "formed-success-cv" app's Modern
  template: sidebar band, skill-level bars, contact links in a list, a footer page number.

Print them again whenever Chromium changes its tagging; the specs read only their structure.
