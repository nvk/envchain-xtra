UNAME = $(shell uname)
CFLAGS += -Wall -Wextra -ansi -pedantic -std=c99
ifeq ($(UNAME), Darwin)
	CFLAGS += -mmacosx-version-min=10.7
	LIBS = -ledit -ltermcap -framework Security -framework CoreFoundation
	OBJS = envchain.o envchain_osx.o
	SWIFTC := $(shell command -v swiftc 2>/dev/null)
	ifneq ($(SWIFTC),)
		EXTRAS = touchid-check
	endif
else
	CFLAGS += `pkg-config --cflags libsecret-1`
	LIBS = -lreadline `pkg-config --libs libsecret-1`
	OBJS = envchain.o envchain_linux.o
endif

DESTDIR ?= /usr

all: envchain $(EXTRAS)
envchain: $(OBJS)
	$(CC) $(LDFLAGS) -o envchain $(OBJS) $(LIBS)

%.o: %.c envchain.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

touchid-check: touchid-check.swift
	$(SWIFTC) -O -o touchid-check touchid-check.swift

clean:
	rm -f envchain touchid-check $(OBJS)

install: all
	install -d $(DESTDIR)/./bin
	install -m755 ./envchain $(DESTDIR)/./bin/envchain
	if test -f ./touchid-check; then install -m755 ./touchid-check $(DESTDIR)/./bin/touchid-check; fi
