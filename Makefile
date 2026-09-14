CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags gtk+-3.0 zlib libzip mupdf)
LIBS = $(shell pkg-config --libs gtk+-3.0 zlib libzip) $(shell pkg-config --libs --static mupdf) -lharfbuzz

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
TARGET = pdf-builder-c

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean
