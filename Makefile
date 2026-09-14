PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
DOCDIR ?= $(PREFIX)/share/doc/ghost-dl
LICENSEDIR ?= $(PREFIX)/share/licenses/ghost-dl
CC ?= gcc
VERSION ?= 2.1.0

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -D_GNU_SOURCE
CPPFLAGS += -DGHOST_VERSION=\"$(VERSION)\" -I src -I vendor -DHAVE_NCURSES

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LDLIBS += -lcurl -lpthread -lncurses -lm
  CPPFLAGS += -I/opt/homebrew/opt/ncurses/include -I/usr/local/opt/ncurses/include
  LDFLAGS += -L/opt/homebrew/opt/ncurses/lib -L/usr/local/opt/ncurses/lib
else
  LDLIBS += -lcurl -lpthread -lncursesw -lm
endif

SRCS := src/main.c src/util.c src/http.c src/parse.c src/download.c src/tui.c src/termimg.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean install uninstall test dist deb rpm pkg

all: ghost-dl

ghost-dl: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

src/%.o: src/%.c src/ghost.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

tests/test_parse: tests/test_parse.c src/util.c src/parse.c src/http.c src/ghost.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_parse.c src/util.c src/parse.c src/http.c -lcurl -lpthread

test: tests/test_parse
	./tests/test_parse

clean:
	rm -f $(OBJS) ghost-dl tests/test_parse
	rm -rf dist

install: ghost-dl
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 ghost-dl $(DESTDIR)$(BINDIR)/ghost-dl
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 644 packaging/ghost-dl.1 $(DESTDIR)$(MANDIR)/man1/ghost-dl.1
	install -d $(DESTDIR)$(DOCDIR)
	install -m 644 README.md $(DESTDIR)$(DOCDIR)/README.md
	install -d $(DESTDIR)$(LICENSEDIR)
	install -m 644 LICENSE $(DESTDIR)$(LICENSEDIR)/LICENSE

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ghost-dl
	rm -f $(DESTDIR)$(MANDIR)/man1/ghost-dl.1
	rm -rf $(DESTDIR)$(DOCDIR)
	rm -rf $(DESTDIR)$(LICENSEDIR)

dist:
	./packaging/build-packages.sh all

deb:
	./packaging/build-packages.sh deb

rpm:
	./packaging/build-packages.sh rpm

pkg:
	./packaging/build-packages.sh pkg
