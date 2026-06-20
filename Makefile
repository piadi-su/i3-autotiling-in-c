CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=gnu11

TARGET = autotiling
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): src/autotiling.c
	$(CC) $(CFLAGS) -o $@ $<

install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)
