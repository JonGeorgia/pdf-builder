CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags gtk+-3.0 zlib libzip mupdf)
LDFLAGS = -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$$ORIGIN/../lib' -Wl,-rpath,'$$ORIGIN/lib' -Wl,--enable-new-dtags
LIBS = $(shell pkg-config --libs gtk+-3.0 zlib libzip) $(shell pkg-config --libs --static mupdf) -lharfbuzz

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
TARGET = pdf-builder-c

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean
