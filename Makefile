CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDFLAGS ?= $(shell pkg-config --libs libcmark-gfm 2>/dev/null || echo -lcmark-gfm -lcmark-gfm-extensions)

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
EMBED   ?= ./embed.sh

GENERATED = default_css.h default_html.h

.PHONY: all clean install uninstall

all: md2pdf

embed.sh:
	chmod +x embed.sh

default_css.h: default.css embed.sh
	$(EMBED) default.css default_css.h DEFAULT_CSS MD2PDF_DEFAULT_CSS_H

default_html.h: default.html embed.sh
	$(EMBED) default.html default_html.h DEFAULT_HTML_TEMPLATE MD2PDF_DEFAULT_HTML_H

md2pdf: md2pdf.c $(GENERATED)
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libcmark-gfm 2>/dev/null) -o $@ md2pdf.c $(LDFLAGS)

clean:
	rm -f md2pdf $(GENERATED)

install: md2pdf
	install -d "$(BINDIR)"
	install -m 755 md2pdf "$(BINDIR)/md2pdf"

uninstall:
	rm -f "$(BINDIR)/md2pdf"
