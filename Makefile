UNAME = $(shell uname)
CFLAGS += -Wall -Wextra -ansi -pedantic -std=c99
ifeq ($(UNAME), Darwin)
	CFLAGS += -mmacosx-version-min=10.7
	LIBS = -ledit -ltermcap -framework Security -framework CoreFoundation
	OBJS = envchain.o envchain_metadata.o envchain_osx.o
else
	CFLAGS += `pkg-config --cflags libsecret-1`
	LIBS = -lreadline `pkg-config --libs libsecret-1`
	OBJS = envchain.o envchain_metadata.o envchain_linux.o
endif

TEST_BIN = tests/test-exec-metadata

DESTDIR ?= /usr

all: envchain
envchain: $(OBJS)
	$(CC) $(LDFLAGS) -o envchain $(OBJS) $(LIBS)

%.o: %.c envchain.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

clean:
	rm -f envchain $(OBJS) $(TEST_BIN)

test: all $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/test-exec-metadata.c envchain_metadata.c envchain_metadata.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -o $@ tests/test-exec-metadata.c envchain_metadata.c

install: all
	install -d $(DESTDIR)/./bin
	install -m755 ./envchain $(DESTDIR)/./bin/envchain
