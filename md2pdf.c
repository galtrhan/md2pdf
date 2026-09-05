#define _POSIX_C_SOURCE 200809L

#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "default_css.h"
#include "default_html.h"

static void die(const char *msg) __attribute__((noreturn));
static void die_errno(const char *msg) __attribute__((noreturn));

static void die(const char *msg) {
  fprintf(stderr, "md2pdf: %s\n", msg);
  exit(1);
}

static void die_errno(const char *msg) {
  fprintf(stderr, "md2pdf: %s: %s\n", msg, strerror(errno));
  exit(1);
}

static void usage(void) {
  fputs(
      "Usage:\n"
      "  md2pdf [--stylesheet FILE] INPUT [OUTPUT.pdf]\n"
      "  md2pdf --help\n"
      "\n"
      "Convert Markdown to PDF with Chrome or Chromium.\n"
      "\n"
      "Options:\n"
      "  -s, --stylesheet FILE  Extra CSS (merged after built-in defaults)\n"
      "  --chrome PATH          Browser executable (overrides env)\n"
      "  -v, --verbose          Show Chrome output (for debugging)\n"
      "  -h, --help             Show this help\n"
      "\n"
      "INPUT may omit the .md extension. OUTPUT defaults to INPUT with a .pdf\n"
      "extension in the same directory. Missing parent directories are created.\n"
      "\n"
      "Page header and footer come from YAML front matter at the top of INPUT:\n"
      "\n"
      "  ---\n"
      "  company: Your Company\n"
      "  client: Client name\n"
      "  logo: assets/logo.svg\n"
      "  date: 2026-09-03\n"
      "  reference: DOC-001\n"
      "  footer: hello@example.com · +1 555 0100\n"
      "  footer-pages: true\n"
      "  ---\n"
      "\n"
      "Set MD2PDF_CHROME or CV_CHROME when the browser is not on PATH.\n",
      stdout);
}

static int ends_with_ci(const char *haystack, const char *suffix);
static char *escape_html(const char *text);
static char *which_executable(const char *name);

static int is_regular_file(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char *read_file(const char *path, size_t *out_len) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return NULL;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }

  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return NULL;
  }

  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }

  char *buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }

  size_t read = fread(buf, 1, (size_t)size, fp);
  fclose(fp);
  if (read != (size_t)size) {
    free(buf);
    return NULL;
  }

  buf[size] = '\0';
  if (out_len) {
    *out_len = (size_t)size;
  }
  return buf;
}

typedef struct {
  char *title;
  char *logo;
  char *company;
  char *client;
  char *subtitle;
  char *date;
  char *reference;
  char *footer;
  char *email;
  char *phone;
  char *website;
  int footer_pages;
} DocMeta;

static void doc_meta_init(DocMeta *meta) {
  memset(meta, 0, sizeof(*meta));
}

static void doc_meta_free(DocMeta *meta) {
  char **fields[] = {
      &meta->title,
      &meta->logo,
      &meta->company,
      &meta->client,
      &meta->subtitle,
      &meta->date,
      &meta->reference,
      &meta->footer,
      &meta->email,
      &meta->phone,
      &meta->website,
  };

  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
    free(*fields[i]);
    *fields[i] = NULL;
  }
  meta->footer_pages = 0;
}

static void meta_set_string(char **field, const char *value) {
  free(*field);
  *field = strdup(value);
}

static char *trim_value(char *value) {
  while (*value == ' ' || *value == '\t') {
    value++;
  }
  size_t len = strlen(value);
  while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t' ||
                     value[len - 1] == '\r')) {
    value[--len] = '\0';
  }
  if (len >= 2 &&
      ((value[0] == '"' && value[len - 1] == '"') ||
       (value[0] == '\'' && value[len - 1] == '\''))) {
    value[len - 1] = '\0';
    return value + 1;
  }
  return value;
}

static int meta_truthy(const char *value) {
  if (!value || !value[0]) {
    return 0;
  }
  return strcmp(value, "0") != 0 &&
         strcmp(value, "false") != 0 &&
         strcmp(value, "no") != 0;
}

static void meta_set_field(DocMeta *meta, const char *key, char *value) {
  char *trimmed = trim_value(value);

  struct {
    const char *key;
    char **field;
  } fields[] = {
      {"title", &meta->title},
      {"logo", &meta->logo},
      {"company", &meta->company},
      {"client", &meta->client},
      {"subtitle", &meta->subtitle},
      {"date", &meta->date},
      {"reference", &meta->reference},
      {"footer", &meta->footer},
      {"email", &meta->email},
      {"phone", &meta->phone},
      {"website", &meta->website},
  };

  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
    if (strcmp(key, fields[i].key) == 0) {
      meta_set_string(fields[i].field, trimmed);
      return;
    }
  }

  if (strcmp(key, "footer-pages") == 0 || strcmp(key, "page-numbers") == 0) {
    meta->footer_pages = meta_truthy(trimmed);
  }
}

static char *split_front_matter(const char *markdown, DocMeta *meta) {
  if (strncmp(markdown, "---\n", 4) != 0) {
    return strdup(markdown);
  }

  const char *end = strstr(markdown + 4, "\n---\n");
  if (!end) {
    return strdup(markdown);
  }

  size_t header_len = (size_t)(end - (markdown + 4));
  char *header = malloc(header_len + 1);
  if (!header) {
    return NULL;
  }
  memcpy(header, markdown + 4, header_len);
  header[header_len] = '\0';

  for (char *line = header; line && *line; ) {
    char *next = strchr(line, '\n');
    if (next) {
      *next = '\0';
      next++;
    }

    char *colon = strchr(line, ':');
    if (colon) {
      *colon = '\0';
      meta_set_field(meta, line, colon + 1);
    }

    line = next;
  }

  free(header);
  return strdup(end + 5);
}

static char *path_dirname(const char *path) {
  char *copy = strdup(path);
  if (!copy) {
    return NULL;
  }
  char *slash = strrchr(copy, '/');
  if (!slash) {
    free(copy);
    return strdup(".");
  }
  if (slash == copy) {
    copy[1] = '\0';
    return copy;
  }
  *slash = '\0';
  return copy;
}

static char *join_path(const char *dir, const char *name) {
  size_t len = strlen(dir) + 1 + strlen(name) + 1;
  char *out = malloc(len);
  if (!out) {
    return NULL;
  }
  snprintf(out, len, "%s/%s", dir, name);
  return out;
}

static const char *logo_mime_type(const char *path) {
  if (ends_with_ci(path, ".png")) {
    return "image/png";
  }
  if (ends_with_ci(path, ".jpg") || ends_with_ci(path, ".jpeg")) {
    return "image/jpeg";
  }
  if (ends_with_ci(path, ".gif")) {
    return "image/gif";
  }
  if (ends_with_ci(path, ".svg")) {
    return "image/svg+xml";
  }
  if (ends_with_ci(path, ".webp")) {
    return "image/webp";
  }
  return NULL;
}

static char *base64_encode(const unsigned char *data, size_t len) {
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out_len = 4 * ((len + 2) / 3);
  char *out = malloc(out_len + 1);
  if (!out) {
    return NULL;
  }

  size_t i = 0;
  size_t j = 0;
  while (i < len) {
    size_t remaining = len - i;
    unsigned char a = data[i++];
    unsigned char b = remaining > 1 ? data[i++] : 0;
    unsigned char c = remaining > 2 ? data[i++] : 0;
    out[j++] = table[a >> 2];
    out[j++] = table[((a & 0x03) << 4) | (b >> 4)];
    out[j++] = remaining > 1 ? table[((b & 0x0f) << 2) | (c >> 6)] : '=';
    out[j++] = remaining > 2 ? table[c & 0x3f] : '=';
  }
  out[j] = '\0';
  return out;
}

static char *load_logo_data_uri(const char *logo_path, const char *input_dir) {
  char *resolved = NULL;
  if (logo_path[0] == '/') {
    resolved = strdup(logo_path);
  } else {
    resolved = join_path(input_dir, logo_path);
  }
  if (!resolved || !is_regular_file(resolved)) {
    free(resolved);
    return NULL;
  }

  const char *mime = logo_mime_type(resolved);
  if (!mime) {
    free(resolved);
    return NULL;
  }

  size_t len = 0;
  char *raw = read_file(resolved, &len);
  free(resolved);
  if (!raw) {
    return NULL;
  }

  char *encoded = base64_encode((const unsigned char *)raw, len);
  free(raw);
  if (!encoded) {
    return NULL;
  }

  size_t uri_len = strlen(mime) + strlen(encoded) + 32;
  char *uri = malloc(uri_len);
  if (!uri) {
    free(encoded);
    return NULL;
  }
  snprintf(uri, uri_len, "data:%s;base64,%s", mime, encoded);
  free(encoded);
  return uri;
}

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} HtmlBuf;

static int html_buf_init(HtmlBuf *buf) {
  buf->cap = 256;
  buf->len = 0;
  buf->data = malloc(buf->cap);
  if (!buf->data) {
    return -1;
  }
  buf->data[0] = '\0';
  return 0;
}

static void html_buf_free(HtmlBuf *buf) {
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

static int html_buf_grow(HtmlBuf *buf, size_t need) {
  if (buf->len + need + 1 <= buf->cap) {
    return 0;
  }
  size_t new_cap = buf->cap;
  while (buf->len + need + 1 > new_cap) {
    new_cap *= 2;
  }
  char *grown = realloc(buf->data, new_cap);
  if (!grown) {
    return -1;
  }
  buf->data = grown;
  buf->cap = new_cap;
  return 0;
}

static int html_buf_append_len(HtmlBuf *buf, const char *text, size_t len) {
  if (html_buf_grow(buf, len) != 0) {
    return -1;
  }
  memcpy(buf->data + buf->len, text, len);
  buf->len += len;
  buf->data[buf->len] = '\0';
  return 0;
}

static int html_buf_append(HtmlBuf *buf, const char *text) {
  size_t add = strlen(text);
  if (html_buf_grow(buf, add) != 0) {
    return -1;
  }
  memcpy(buf->data + buf->len, text, add);
  buf->len += add;
  buf->data[buf->len] = '\0';
  return 0;
}

static int html_buf_append_escaped(HtmlBuf *buf, const char *text) {
  char *escaped = escape_html(text);
  if (!escaped) {
    return -1;
  }
  int ok = html_buf_append(buf, escaped);
  free(escaped);
  return ok;
}

static int meta_has_header(const DocMeta *meta) {
  return meta->logo || meta->company || meta->client || meta->subtitle ||
         meta->date || meta->reference;
}

static int meta_has_footer(const DocMeta *meta) {
  return meta->footer || meta->email || meta->phone || meta->website || meta->footer_pages;
}

static char *build_footer_contact(const DocMeta *meta) {
  if (meta->footer && meta->footer[0]) {
    return strdup(meta->footer);
  }

  HtmlBuf buf;
  if (html_buf_init(&buf) != 0) {
    return NULL;
  }

  const char *parts[] = {meta->email, meta->phone, meta->website, NULL};
  for (size_t i = 0; parts[i]; i++) {
    if (!parts[i][0]) {
      continue;
    }
    if (buf.len > 0 && html_buf_append(&buf, " · ") != 0) {
      html_buf_free(&buf);
      return NULL;
    }
    if (html_buf_append(&buf, parts[i]) != 0) {
      html_buf_free(&buf);
      return NULL;
    }
  }

  if (buf.len == 0) {
    html_buf_free(&buf);
    return strdup("");
  }

  char *out = buf.data;
  return out;
}

static char *build_header_html(const DocMeta *meta, const char *input_dir) {
  if (!meta_has_header(meta)) {
    return NULL;
  }

  HtmlBuf buf;
  if (html_buf_init(&buf) != 0) {
    return NULL;
  }

  if (html_buf_append(&buf, "<header class=\"doc-header\" aria-hidden=\"true\">\n"
                            "  <div class=\"doc-header__inner\">\n") != 0) {
    html_buf_free(&buf);
    return NULL;
  }

  if (meta->logo || meta->company || meta->client) {
    if (html_buf_append(&buf, "    <div class=\"doc-header__brand\">\n") != 0) {
      html_buf_free(&buf);
      return NULL;
    }

    if (meta->logo) {
      char *logo_uri = load_logo_data_uri(meta->logo, input_dir);
      if (logo_uri) {
        if (html_buf_append(&buf, "      <img class=\"doc-header__logo\" src=\"") != 0 ||
            html_buf_append(&buf, logo_uri) != 0 ||
            html_buf_append(&buf, "\" alt=\"\">\n") != 0) {
          free(logo_uri);
          html_buf_free(&buf);
          return NULL;
        }
        free(logo_uri);
      }
    }

    if (meta->company || meta->client || meta->subtitle) {
      if (html_buf_append(&buf, "      <div class=\"doc-header__text\">\n") != 0) {
        html_buf_free(&buf);
        return NULL;
      }
      if (meta->company) {
        if (html_buf_append(&buf, "        <span class=\"doc-header__company\">") != 0 ||
            html_buf_append_escaped(&buf, meta->company) != 0 ||
            html_buf_append(&buf, "</span>\n") != 0) {
          html_buf_free(&buf);
          return NULL;
        }
      }
      if (meta->subtitle) {
        if (html_buf_append(&buf, "        <div class=\"doc-header__client\">") != 0 ||
            html_buf_append_escaped(&buf, meta->subtitle) != 0 ||
            html_buf_append(&buf, "</div>\n") != 0) {
          html_buf_free(&buf);
          return NULL;
        }
      } else if (meta->client) {
        if (html_buf_append(&buf, "        <div class=\"doc-header__client\">") != 0 ||
            html_buf_append(&buf, "Proposal for ") != 0 ||
            html_buf_append_escaped(&buf, meta->client) != 0 ||
            html_buf_append(&buf, "</div>\n") != 0) {
          html_buf_free(&buf);
          return NULL;
        }
      }
      if (html_buf_append(&buf, "      </div>\n") != 0) {
        html_buf_free(&buf);
        return NULL;
      }
    }

    if (html_buf_append(&buf, "    </div>\n") != 0) {
      html_buf_free(&buf);
      return NULL;
    }
  }

  if (meta->date || meta->reference) {
    if (html_buf_append(&buf, "    <div class=\"doc-header__meta\">\n") != 0) {
      html_buf_free(&buf);
      return NULL;
    }
    if (meta->date) {
      if (html_buf_append(&buf, "      <div>") != 0 ||
          html_buf_append_escaped(&buf, meta->date) != 0 ||
          html_buf_append(&buf, "</div>\n") != 0) {
        html_buf_free(&buf);
        return NULL;
      }
    }
    if (meta->reference) {
      if (html_buf_append(&buf, "      <div>") != 0 ||
          html_buf_append_escaped(&buf, meta->reference) != 0 ||
          html_buf_append(&buf, "</div>\n") != 0) {
        html_buf_free(&buf);
        return NULL;
      }
    }
    if (html_buf_append(&buf, "    </div>\n") != 0) {
      html_buf_free(&buf);
      return NULL;
    }
  }

  if (html_buf_append(&buf, "  </div>\n</header>\n") != 0) {
    html_buf_free(&buf);
    return NULL;
  }

  return buf.data;
}

static char *build_footer_html(const DocMeta *meta) {
  if (!meta_has_footer(meta)) {
    return NULL;
  }

  char *contact = build_footer_contact(meta);
  if (!contact) {
    return NULL;
  }

  HtmlBuf buf;
  if (html_buf_init(&buf) != 0) {
    free(contact);
    return NULL;
  }

  if (html_buf_append(&buf, "<footer class=\"doc-footer\" aria-hidden=\"true\">\n"
                            "  <div class=\"doc-footer__inner\">\n") != 0) {
    free(contact);
    html_buf_free(&buf);
    return NULL;
  }

  if (contact[0]) {
    if (html_buf_append(&buf, "    <span class=\"doc-footer__contact\">") != 0 ||
        html_buf_append_escaped(&buf, contact) != 0 ||
        html_buf_append(&buf, "</span>\n") != 0) {
      free(contact);
      html_buf_free(&buf);
      return NULL;
    }
  } else {
    if (html_buf_append(&buf, "    <span class=\"doc-footer__contact\"></span>\n") != 0) {
      free(contact);
      html_buf_free(&buf);
      return NULL;
    }
  }
  free(contact);

  if (html_buf_append(&buf, "  </div>\n</footer>\n") != 0) {
    html_buf_free(&buf);
    return NULL;
  }

  return buf.data;
}

static char *resolve_input(const char *path) {
  if (is_regular_file(path)) {
    return strdup(path);
  }

  size_t base_len = strlen(path);
  char *candidate = malloc(base_len + 16);
  if (!candidate) {
    return NULL;
  }

  snprintf(candidate, base_len + 5, "%s.md", path);
  if (is_regular_file(candidate)) {
    return candidate;
  }

  snprintf(candidate, base_len + 11, "%s.markdown", path);
  if (is_regular_file(candidate)) {
    return candidate;
  }

  free(candidate);
  return NULL;
}

static char *strip_markdown_ext(const char *path) {
  size_t len = strlen(path);
  if (len > 3 && strcmp(path + len - 3, ".md") == 0) {
    char *out = malloc(len - 2);
    if (!out) {
      return NULL;
    }
    memcpy(out, path, len - 3);
    out[len - 3] = '\0';
    return out;
  }
  if (len > 9 && strcmp(path + len - 9, ".markdown") == 0) {
    char *out = malloc(len - 8);
    if (!out) {
      return NULL;
    }
    memcpy(out, path, len - 9);
    out[len - 9] = '\0';
    return out;
  }
  return strdup(path);
}

static char *default_output_path(const char *input_path) {
  char *base = strip_markdown_ext(input_path);
  if (!base) {
    return NULL;
  }

  size_t len = strlen(base);
  char *out = malloc(len + 5);
  if (!out) {
    free(base);
    return NULL;
  }
  snprintf(out, len + 5, "%s.pdf", base);
  free(base);
  return out;
}

static int ends_with_ci(const char *haystack, const char *suffix) {
  size_t hlen = strlen(haystack);
  size_t slen = strlen(suffix);
  if (hlen < slen) {
    return 0;
  }
  const char *start = haystack + hlen - slen;
  for (size_t i = 0; i < slen; i++) {
    char a = start[i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') {
      a += 'a' - 'A';
    }
    if (b >= 'A' && b <= 'Z') {
      b += 'a' - 'A';
    }
    if (a != b) {
      return 0;
    }
  }
  return 1;
}

static char *ensure_pdf_extension(const char *path) {
  if (ends_with_ci(path, ".pdf")) {
    return strdup(path);
  }
  size_t len = strlen(path);
  char *out = malloc(len + 5);
  if (!out) {
    return NULL;
  }
  snprintf(out, len + 5, "%s.pdf", path);
  return out;
}

static int mkdir_p(const char *path) {
  char buf[PATH_MAX];
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof(buf)) {
    return -1;
  }
  memcpy(buf, path, len + 1);

  for (char *p = buf + 1; *p; p++) {
    if (*p != '/') {
      continue;
    }
    *p = '\0';
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
      return -1;
    }
    *p = '/';
  }

  if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
    return -1;
  }
  return 0;
}

static char *absolute_path(const char *path) {
  if (path[0] == '/') {
    return strdup(path);
  }

  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) {
    return NULL;
  }

  return join_path(cwd, path);
}

static char *resolve_chrome_path(const char *name) {
  if (access(name, X_OK) == 0) {
    return strdup(name);
  }
  return which_executable(name);
}

static char *which_executable(const char *name) {
  const char *path_env = getenv("PATH");
  if (!path_env) {
    return NULL;
  }

  char *paths = strdup(path_env);
  if (!paths) {
    return NULL;
  }

  char *save = NULL;
  for (char *dir = strtok_r(paths, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
    size_t len = strlen(dir) + 1 + strlen(name) + 1;
    char *candidate = malloc(len);
    if (!candidate) {
      continue;
    }
    snprintf(candidate, len, "%s/%s", dir, name);
    if (access(candidate, X_OK) == 0) {
      free(paths);
      return candidate;
    }
    free(candidate);
  }

  free(paths);
  return NULL;
}

static char *find_chrome(const char *override) {
  if (override && override[0]) {
    char *found = resolve_chrome_path(override);
    if (found) {
      return found;
    }
    die("Chrome executable is not runnable");
  }

  const char *env = getenv("MD2PDF_CHROME");
  if (!env || !env[0]) {
    env = getenv("CV_CHROME");
  }
  if (env && env[0]) {
    char *found = resolve_chrome_path(env);
    if (found) {
      return found;
    }
  }

  static const char *candidates[] = {
      "google-chrome",
      "google-chrome-stable",
      "chromium",
      "chromium-browser",
      NULL,
  };

  for (size_t i = 0; candidates[i]; i++) {
    char *found = which_executable(candidates[i]);
    if (found) {
      return found;
    }
  }

  die("Chrome or Chromium is required. Set MD2PDF_CHROME or CV_CHROME.");
  return NULL;
}

static char *escape_html(const char *text) {
  size_t cap = 64;
  size_t len = 0;
  char *out = malloc(cap);
  if (!out) {
    return NULL;
  }

  for (const char *p = text; *p; p++) {
    const char *rep = NULL;
    switch (*p) {
      case '&':
        rep = "&amp;";
        break;
      case '<':
        rep = "&lt;";
        break;
      case '>':
        rep = "&gt;";
        break;
      case '"':
        rep = "&quot;";
        break;
      case '\'':
        rep = "&#39;";
        break;
      default:
        break;
    }

    if (rep) {
      size_t need = len + strlen(rep) + 1;
      if (need > cap) {
        cap = need * 2;
        char *grown = realloc(out, cap);
        if (!grown) {
          free(out);
          return NULL;
        }
        out = grown;
      }
      memcpy(out + len, rep, strlen(rep));
      len += strlen(rep);
      out[len] = '\0';
      continue;
    }

    if (len + 2 > cap) {
      cap *= 2;
      char *grown = realloc(out, cap);
      if (!grown) {
        free(out);
        return NULL;
      }
      out = grown;
    }
    out[len++] = *p;
    out[len] = '\0';
  }

  return out;
}

static char *extract_title(const char *markdown) {
  const char *line_start = markdown;
  while (*line_start) {
    const char *line_end = strchr(line_start, '\n');
    size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);

    if (line_len >= 2 && line_start[0] == '#' && line_start[1] == ' ') {
      const char *title_start = line_start + 2;
      size_t title_len = line_len - 2;
      while (title_len > 0 && (title_start[title_len - 1] == ' ' || title_start[title_len - 1] == '\r')) {
        title_len--;
      }
      char *title = malloc(title_len + 1);
      if (!title) {
        return NULL;
      }
      memcpy(title, title_start, title_len);
      title[title_len] = '\0';
      return title;
    }

    if (!line_end) {
      break;
    }
    line_start = line_end + 1;
  }

  return strdup("Document");
}

static int ci_equal(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') {
      ca += 'a' - 'A';
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb += 'a' - 'A';
    }
    if (ca != cb) {
      return 0;
    }
  }
  return 1;
}

static char *apply_page_breaks(const char *markdown) {
  static const char marker[] = "<div class=\"page-break\"></div>";
  size_t in_len = strlen(markdown);
  size_t cap = in_len + 64;
  char *out = malloc(cap);
  if (!out) {
    return NULL;
  }

  size_t out_len = 0;
  size_t i = 0;
  while (i < in_len) {
    if (markdown[i] == '<' && i + 4 < in_len && markdown[i + 1] == '!' &&
        markdown[i + 2] == '-' && markdown[i + 3] == '-') {
      size_t j = i + 4;
      while (j < in_len && (markdown[j] == ' ' || markdown[j] == '\t')) {
        j++;
      }

      size_t k = j;
      while (k < in_len && markdown[k] != '-' && markdown[k] != '\n' && markdown[k] != '\r') {
        k++;
      }

      size_t token_len = k - j;
      int is_break = 0;
      if (token_len >= 9 && ci_equal(markdown + j, "pagebreak", 9)) {
        is_break = 1;
      } else if (token_len >= 10 && ci_equal(markdown + j, "page-break", 10)) {
        is_break = 1;
      } else if (token_len >= 10 && ci_equal(markdown + j, "page break", 10)) {
        is_break = 1;
      }

      if (is_break) {
        while (k < in_len && markdown[k] != '\n') {
          k++;
        }
        if (k + 2 < in_len && markdown[k] == '\n' && markdown[k + 1] == '-' &&
            markdown[k + 2] == '-') {
          k += 3;
          while (k < in_len && markdown[k] != '>' && markdown[k] != '\n') {
            k++;
          }
          if (k < in_len && markdown[k] == '>') {
            k++;
          }
        }

        size_t need = out_len + sizeof(marker) + 1;
        if (need > cap) {
          cap = need * 2;
          char *grown = realloc(out, cap);
          if (!grown) {
            free(out);
            return NULL;
          }
          out = grown;
        }
        memcpy(out + out_len, marker, sizeof(marker) - 1);
        out_len += sizeof(marker) - 1;
        i = k;
        continue;
      }
    }

    if (out_len + 1 >= cap) {
      cap *= 2;
      char *grown = realloc(out, cap);
      if (!grown) {
        free(out);
        return NULL;
      }
      out = grown;
    }
    out[out_len++] = markdown[i++];
  }

  out[out_len] = '\0';
  return out;
}

static void attach_gfm_extensions(cmark_parser *parser) {
  static const char *names[] = {
      "table",
      "strikethrough",
      "autolink",
      "tagfilter",
      "tasklist",
      NULL,
  };

  for (size_t i = 0; names[i]; i++) {
    cmark_syntax_extension *extension = cmark_find_syntax_extension(names[i]);
    if (extension) {
      cmark_parser_attach_syntax_extension(parser, extension);
    }
  }
}

static char *markdown_to_html(const char *markdown) {
  char *prepared = apply_page_breaks(markdown);
  if (!prepared) {
    return NULL;
  }

  cmark_gfm_core_extensions_ensure_registered();

  cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
  if (!parser) {
    free(prepared);
    return NULL;
  }

  attach_gfm_extensions(parser);

  cmark_parser_feed(parser, prepared, strlen(prepared));
  cmark_node *document = cmark_parser_finish(parser);
  free(prepared);

  if (!document) {
    cmark_parser_free(parser);
    return NULL;
  }

  char *html = cmark_render_html(
      document,
      CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE,
      NULL);

  cmark_node_free(document);
  cmark_parser_free(parser);
  return html;
}

typedef struct {
  const char *token;
  const char *value;
} TemplatePart;

static const char *template_value(const TemplatePart *parts, size_t count, const char *token) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(parts[i].token, token) == 0) {
      return parts[i].value ? parts[i].value : "";
    }
  }
  return "";
}

static char *render_template(const char *tmpl, const TemplatePart *parts, size_t count) {
  HtmlBuf buf;
  if (html_buf_init(&buf) != 0) {
    return NULL;
  }

  const char *cursor = tmpl;
  while (*cursor) {
    const char *start = strstr(cursor, "{{");
    if (!start) {
      if (html_buf_append(&buf, cursor) != 0) {
        html_buf_free(&buf);
        return NULL;
      }
      break;
    }

    if (start > cursor &&
        html_buf_append_len(&buf, cursor, (size_t)(start - cursor)) != 0) {
      html_buf_free(&buf);
      return NULL;
    }

    const char *end = strstr(start, "}}");
    if (!end) {
      if (html_buf_append(&buf, start) != 0) {
        html_buf_free(&buf);
        return NULL;
      }
      break;
    }

    char token[64];
    size_t token_len = (size_t)(end - start + 2);
    if (token_len >= sizeof(token)) {
      html_buf_free(&buf);
      return NULL;
    }
    memcpy(token, start, token_len);
    token[token_len] = '\0';

    if (html_buf_append(&buf, template_value(parts, count, token)) != 0) {
      html_buf_free(&buf);
      return NULL;
    }

    cursor = end + 2;
  }

  return buf.data;
}

static char *build_html_document(
    const char *title,
    const char *html_attrs,
    const char *doc_class,
    const char *header_html,
    const char *footer_html,
    const char *body_html,
    const char *extra_css) {
  TemplatePart parts[] = {
      {"{{TITLE}}", title},
      {"{{HTML_ATTRS}}", html_attrs ? html_attrs : ""},
      {"{{DEFAULT_CSS}}", DEFAULT_CSS},
      {"{{EXTRA_CSS}}", extra_css ? extra_css : ""},
      {"{{HEADER}}", header_html ? header_html : ""},
      {"{{DOC_CLASS}}", doc_class},
      {"{{BODY}}", body_html},
      {"{{FOOTER}}", footer_html ? footer_html : ""},
  };

  return render_template(
      DEFAULT_HTML_TEMPLATE,
      parts,
      sizeof(parts) / sizeof(parts[0]));
}

static char *file_uri(const char *path) {
  size_t len = strlen(path);
  char *uri = malloc(len + 16);
  if (!uri) {
    return NULL;
  }
  snprintf(uri, len + 16, "file://%s", path);
  return uri;
}

static void silence_stdio(void) {
  int devnull = open("/dev/null", O_WRONLY);
  if (devnull < 0) {
    return;
  }
  dup2(devnull, STDOUT_FILENO);
  dup2(devnull, STDERR_FILENO);
  close(devnull);
}

static int run_chrome_pdf(
    const char *chrome,
    const char *html_path,
    const char *pdf_path,
    int verbose) {
  char *uri = file_uri(html_path);
  if (!uri) {
    return -1;
  }

  char pdf_arg[PATH_MAX + 32];
  snprintf(pdf_arg, sizeof(pdf_arg), "--print-to-pdf=%s", pdf_path);

  int as_root = geteuid() == 0;
  char *argv[10];
  int argc = 0;
  argv[argc++] = (char *)chrome;
  argv[argc++] = "--headless";
  argv[argc++] = "--disable-gpu";
  if (as_root) {
    argv[argc++] = "--no-sandbox";
  }
  argv[argc++] = "--no-pdf-header-footer";
  argv[argc++] = pdf_arg;
  argv[argc++] = uri;
  argv[argc] = NULL;

  pid_t pid = fork();
  if (pid < 0) {
    free(uri);
    return -1;
  }

  if (pid == 0) {
    if (!verbose) {
      silence_stdio();
    }
    execv(chrome, argv);
    _exit(127);
  }

  free(uri);

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  const char *stylesheet = NULL;
  const char *chrome_override = NULL;
  const char *input_arg = NULL;
  const char *output_arg = NULL;
  int verbose = 0;

  const char *verbose_env = getenv("MD2PDF_VERBOSE");
  if (verbose_env && verbose_env[0] && strcmp(verbose_env, "0") != 0) {
    verbose = 1;
  }

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      usage();
      return 0;
    }
    if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
      verbose = 1;
      continue;
    }
    if (strcmp(arg, "--stylesheet") == 0 || strcmp(arg, "-s") == 0) {
      if (i + 1 >= argc) {
        die("missing value for --stylesheet");
      }
      stylesheet = argv[++i];
      continue;
    }
    if (strcmp(arg, "--chrome") == 0) {
      if (i + 1 >= argc) {
        die("missing value for --chrome");
      }
      chrome_override = argv[++i];
      continue;
    }
    if (arg[0] == '-') {
      die("unknown option (use --help)");
    }
    if (!input_arg) {
      input_arg = arg;
    } else if (!output_arg) {
      output_arg = arg;
    } else {
      usage();
      return 1;
    }
  }

  if (!input_arg) {
    usage();
    return 1;
  }

  char *input_path = resolve_input(input_arg);
  if (!input_path) {
    fprintf(stderr, "md2pdf: input file not found: %s\n", input_arg);
    return 1;
  }

  char *output_path = NULL;
  if (output_arg) {
    output_path = ensure_pdf_extension(output_arg);
  } else {
    output_path = default_output_path(input_path);
  }
  if (!output_path) {
    free(input_path);
    die_errno("output path");
  }

  char *input_abs = absolute_path(input_path);
  char *output_abs = absolute_path(output_path);
  free(input_path);
  free(output_path);
  if (!input_abs || !output_abs) {
    free(input_abs);
    free(output_abs);
    die_errno("path resolution");
  }

  char *output_dir = path_dirname(output_abs);
  if (!output_dir) {
    free(input_abs);
    free(output_abs);
    die_errno("output directory");
  }
  if (strcmp(output_dir, ".") != 0 && mkdir_p(output_dir) != 0) {
    free(output_dir);
    free(input_abs);
    free(output_abs);
    die_errno("create output directory");
  }
  free(output_dir);

  char *markdown = read_file(input_abs, NULL);
  if (!markdown) {
    free(input_abs);
    free(output_abs);
    die_errno("read input");
  }

  DocMeta meta;
  doc_meta_init(&meta);
  char *markdown_body = split_front_matter(markdown, &meta);
  free(markdown);
  if (!markdown_body) {
    doc_meta_free(&meta);
    free(input_abs);
    free(output_abs);
    die("front matter");
  }

  char *input_dir = path_dirname(input_abs);
  if (!input_dir) {
    free(markdown_body);
    doc_meta_free(&meta);
    free(input_abs);
    free(output_abs);
    die_errno("input directory");
  }

  char *title_raw = meta.title ? strdup(meta.title) : extract_title(markdown_body);
  char *title = title_raw ? escape_html(title_raw) : NULL;
  free(title_raw);
  if (!title) {
    free(input_dir);
    free(markdown_body);
    doc_meta_free(&meta);
    free(input_abs);
    free(output_abs);
    die("document title");
  }

  char *body_html = markdown_to_html(markdown_body);
  free(markdown_body);
  if (!body_html) {
    free(title);
    free(input_dir);
    doc_meta_free(&meta);
    free(input_abs);
    free(output_abs);
    die("markdown conversion");
  }

  char *header_html = build_header_html(&meta, input_dir);
  char *footer_html = build_footer_html(&meta);
  const char *doc_class = (header_html || footer_html) ? "doc has-chrome" : "doc";
  const char *html_attrs = meta.footer_pages ? " class=\"has-page-numbers\"" : "";

  char *extra_css = NULL;
  if (stylesheet) {
    extra_css = read_file(stylesheet, NULL);
    if (!extra_css) {
      free(header_html);
      free(footer_html);
      free(body_html);
      free(title);
      free(input_dir);
      doc_meta_free(&meta);
      free(input_abs);
      free(output_abs);
      die_errno("read stylesheet");
    }
  }

  char *document = build_html_document(
      title,
      html_attrs,
      doc_class,
      header_html,
      footer_html,
      body_html,
      extra_css);
  free(header_html);
  free(footer_html);
  free(body_html);
  free(title);
  free(extra_css);
  free(input_dir);
  doc_meta_free(&meta);
  if (!document) {
    free(input_abs);
    free(output_abs);
    die("build HTML");
  }

  char html_template[] = "/tmp/md2pdf-html-XXXXXX";
  int html_fd = mkstemp(html_template);
  if (html_fd < 0) {
    free(document);
    free(input_abs);
    free(output_abs);
    die_errno("temporary file");
  }

  char html_path[PATH_MAX];
  if (snprintf(html_path, sizeof(html_path), "%s.html", html_template) >= (int)sizeof(html_path)) {
    close(html_fd);
    unlink(html_template);
    free(document);
    free(input_abs);
    free(output_abs);
    die("temporary file path too long");
  }

  if (rename(html_template, html_path) != 0) {
    close(html_fd);
    unlink(html_template);
    free(document);
    free(input_abs);
    free(output_abs);
    die_errno("temporary file");
  }

  FILE *html_fp = fdopen(html_fd, "wb");
  if (!html_fp) {
    close(html_fd);
    unlink(html_path);
    free(document);
    free(input_abs);
    free(output_abs);
    die_errno("temporary file");
  }

  size_t doc_len = strlen(document);
  if (fwrite(document, 1, doc_len, html_fp) != doc_len || fclose(html_fp) != 0) {
    unlink(html_path);
    free(document);
    free(input_abs);
    free(output_abs);
    die_errno("write HTML");
  }
  free(document);

  char *chrome = find_chrome(chrome_override);
  if (run_chrome_pdf(chrome, html_path, output_abs, verbose) != 0) {
    unlink(html_path);
    free(chrome);
    free(input_abs);
    free(output_abs);
    die("Chrome did not create a PDF");
  }

  unlink(html_path);
  free(chrome);

  struct stat st;
  if (stat(output_abs, &st) != 0 || st.st_size == 0) {
    free(input_abs);
    free(output_abs);
    die("output PDF is missing or empty");
  }

  printf("Created %s\n", output_abs);

  free(input_abs);
  free(output_abs);
  return 0;
}
