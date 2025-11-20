CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=gnu11
TARGET = autotiling

.PHONY: all clean install

all: $(TARGET)

$(TARGET): autotiling.c
	$(CC) $(CFLAGS) -o $(TARGET) autotiling.c

install: $(TARGET)
	install -d $(HOME)/.local/bin
	install -m 755 $(TARGET) $(HOME)/.local/bin/$(TARGET)

clean:
	rm -f $(TARGET)

