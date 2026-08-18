# Markdown CV to PDF

Convert Markdown CV files to A4 PDF files with Chrome or Chromium.

## Requirements

- Node.js and npm.
- Google Chrome or Chromium.

## Setup

Run these commands after you clone the repository:

```sh
chmod +x cv
./cv --setup
```

The setup command installs dependencies in `node_modules` and creates or updates `package-lock.json`.

## Usage

Generate a PDF next to the input file:

```sh
./cv INPUT.md
```

The script replaces the input extension with `.pdf`.

Generate a PDF with a custom path:

```sh
./cv INPUT.md OUTPUT.pdf
```

The script creates missing output directories. It adds `.pdf` when the custom name has no extension.

Show command help:

```sh
./cv --help
```

The input file must use the `.md` or `.markdown` extension. Relative paths use the current directory.

## Multiple CV Files

Keep tailored files separate:

```text
cv-backend.md
cv-platform.md
cv-full-stack.md
```

Render each file with its own output name:

```sh
./cv cv-backend.md applications/backend.pdf
./cv cv-platform.md applications/platform.pdf
```

The renderer does not change the input file.

## Markdown Format

Use standard Markdown headings and lists. The first level-one heading becomes the document title. The first level-two heading becomes the professional title.

Use this structure:

```md
# Candidate Name

## Backend Engineer

City, Country  
email@example.com  
https://www.linkedin.com/in/example

## Profile

Short professional summary.

## Core Skills

- PHP, Laravel, PHPUnit
- Docker, AWS, MySQL

## Professional Experience

### Company Name

**Backend Engineer** | January 2024 – Present

- Delivered a production feature.
```

The renderer gives two-column styling to the list after an exact `## Core Skills` heading. Other lists use the normal document style.

## Manual Page Breaks

Place this comment on its own line before a section that must start on a new PDF page:

```md
<!-- pagebreak -->
```

The comment stays invisible in Markdown previews. The renderer changes it into a print page break.

## Customization

- Edit `cv.css` for fonts, colors, spacing, margins, and section layout.
- Edit `cv-template.html` for the HTML structure.
- Edit `render-cv.mjs` for new Markdown processing rules.

The default template provides A4 pages, selectable and searchable text, clickable links, print CSS, and standard headings for applicant tracking systems.

## Browser Path

The script checks these executable names:

- `google-chrome`
- `google-chrome-stable`
- `chromium`
- `chromium-browser`

Set `CV_CHROME` when the browser uses another path:

```sh
CV_CHROME=/path/to/chrome ./cv INPUT.md
```

## Troubleshooting

If the script cannot find the input file, pass an existing `.md` or `.markdown` path.

If the script cannot find Chrome or Chromium, install a supported browser or set `CV_CHROME`.

If the command returns a permission error, make the file executable with `chmod +x cv`.

## Git Files

The repository ignores `*.md` and `*.pdf` files to keep private CV content and generated PDFs out of the repository. The tracked `README.md` remains available because `.gitignore` includes a `!README.md` exception.

The repository also ignores `node_modules/`. It tracks the dependency definition and lock file.

## License

This project uses the MIT License. See `LICENSE` for the full license text.
