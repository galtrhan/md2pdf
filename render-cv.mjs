import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { marked } from 'marked';

const [inputPath, outputPath] = process.argv.slice(2);

if (!inputPath || !outputPath) {
  throw new Error('Usage: node render-cv.mjs INPUT.md OUTPUT.html');
}

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const markdown = await readFile(inputPath, 'utf8');
const template = await readFile(path.join(scriptDirectory, 'cv-template.html'), 'utf8');
const css = await readFile(path.join(scriptDirectory, 'cv.css'), 'utf8');

const titleMatch = markdown.match(/^#\s+(.+)$/m);
const title = titleMatch ? titleMatch[1].trim() : 'Curriculum Vitae';

const renderedMarkdown = marked.parse(markdown, {
  breaks: false,
  gfm: true,
});

const content = renderedMarkdown
  .replace(
    /<!--\s*page[- ]?break\s*-->/gi,
    '<div class="page-break" aria-hidden="true"></div>',
  )
  .replace(
  /(<h2>Core Skills<\/h2>\s*)<ul>/,
  '$1<ul class="skills">',
  );

const escapeHtml = (value) => value
  .replaceAll('&', '&amp;')
  .replaceAll('<', '&lt;')
  .replaceAll('>', '&gt;')
  .replaceAll('"', '&quot;')
  .replaceAll("'", '&#39;');

const html = template
  .replace('{{CV_TITLE}}', () => escapeHtml(title))
  .replace('{{CV_CSS}}', () => css)
  .replace('{{CV_CONTENT}}', () => content);

await writeFile(outputPath, html, 'utf8');
