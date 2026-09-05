# Markdown to PDF

`md2pdf` converts Markdown files to A4 PDF files. It uses Chrome or Chromium to print the PDF.

## Requirements

You need these items:

- A C compiler (`cc` or `gcc`)
- [libcmark-gfm](https://github.com/github/cmark-gfm) development files
- Google Chrome or Chromium

On Arch Linux, install libcmark-gfm:

```sh
sudo pacman -S cmark-gfm
```

On Debian or Ubuntu, install libcmark-gfm development files:

```sh
sudo apt install libcmark-gfm-dev
```

## Build

Run this command in the repository root:

```sh
make
```

The build creates the `md2pdf` binary (about 26 KB). It embeds `default.css` and `default.html` in the binary.

Edit `default.css` to change default typography, tables, headers, and print layout.

Edit `default.html` to change the HTML shell. Use these placeholders:

| Placeholder | Value |
| ----------- | ----- |
| `{{TITLE}}` | Document title |
| `{{HTML_ATTRS}}` | Extra attributes on `<html>` (e.g. page-number class) |
| `{{DEFAULT_CSS}}` | Embedded CSS from `default.css` |
| `{{EXTRA_CSS}}` | CSS from `--stylesheet` |
| `{{HEADER}}` | Header HTML from front matter |
| `{{DOC_CLASS}}` | `doc` or `doc has-chrome` |
| `{{BODY}}` | Rendered Markdown |
| `{{FOOTER}}` | Footer HTML from front matter |

Run `make` again after you change `default.css` or `default.html`.

Install the binary to `/usr/local/bin`:

```sh
make install
```

To install to `/usr/bin`, run:

```sh
make install PREFIX=/usr
```

## Usage

```sh
./md2pdf INPUT.md
./md2pdf INPUT.md OUTPUT.pdf
./md2pdf --stylesheet cv.css INPUT
./md2pdf -s proposal.css notes.md out/proposal.pdf
./md2pdf --template custom.html INPUT.md
./md2pdf -t custom.html -s cv.css INPUT.md
```

`INPUT` can omit the `.md` extension. The tool checks these names in order:

1. `INPUT`
2. `INPUT.md`
3. `INPUT.markdown`

If you omit `OUTPUT`, the tool writes `INPUT.pdf` in the same directory as the input file.

If `OUTPUT` has no `.pdf` extension, the tool adds `.pdf`.

Show command help:

```sh
./md2pdf --help
```

## Page Header and Footer

Put YAML front matter at the top of the Markdown file:

```yaml
---
company: Example Ltd
client: Acme Corp
subtitle: Custom line under the company name
logo: assets/logo.svg
date: 2026-09-03
reference: DOC-001
footer: hello@example.com · +1 555 0100
email: hello@example.com
phone: +1 555 0100
website: https://example.com
footer-pages: true
---
```

### Front Matter Fields

| Field | Purpose |
| ----- | ------- |
| `title` | Document title. It replaces the first `#` heading. |
| `logo` | Image path. The path is relative to the Markdown file. |
| `company` | Main header line on the left |
| `client` | Left header subtitle as `Proposal for …` |
| `subtitle` | Left header subtitle as free text. It replaces `client`. |
| `date` | Right header line |
| `reference` | Right header line |
| `footer` | Left footer text |
| `email` | Footer text when `footer` is empty |
| `phone` | Footer text when `footer` is empty |
| `website` | Footer text when `footer` is empty |
| `footer-pages` | Page numbers on the right (`1 / 4`) |

The tool joins `email`, `phone`, and `website` with ` · ` when `footer` is empty.

Use `proposal.css` or your own stylesheet to style `.doc-header` and `.doc-footer`.

## Layout and styling

The build compiles `default.css` into the binary. That file sets default typography and layout.

The build compiles `default.html` into the binary. That file sets the HTML document structure.

Use `--stylesheet` or `-s` to add CSS after the defaults.

Use `--template` or `-t` to replace the built-in HTML shell with your own file. The file must use the same placeholders as `default.html` (see above).

This repository includes:

- `cv.css` — CV layout
- `proposal.css` — proposal layout with header and footer spacing

## Page Breaks

Put this HTML comment on its own line before a section that must start on a new page:

```md
<!-- pagebreak -->
```

These variants also work: `page-break`, `page break`.

## Browser Path

The tool searches for these program names on `PATH`:

- `google-chrome`
- `google-chrome-stable`
- `chromium`
- `chromium-browser`

Set `MD2PDF_CHROME` when the browser is not on `PATH`:

```sh
MD2PDF_CHROME=/path/to/chromium ./md2pdf INPUT.md
```

You can also pass `--chrome /path/to/chromium`.

## CV Workflow

Keep one Markdown file per CV. Render each file separately:

```sh
./md2pdf --stylesheet cv.css cv-backend.md applications/backend.pdf
./md2pdf --stylesheet cv.css cv-platform.md applications/platform.pdf
```

The first `#` heading becomes the document title.

## Git Files

The repository ignores `*.md` and `*.pdf` files. `README.md` is not ignored.

The repository also ignores the built `md2pdf` binary.

## License

This project uses the MIT License. See `LICENSE`.
