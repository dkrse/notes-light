CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -O2 $(shell pkg-config --cflags libadwaita-1)
LDFLAGS = $(shell pkg-config --libs libadwaita-1)

BUILDDIR = build
SRC = src/main.c src/window.c src/settings.c src/actions.c src/ssh.c
OBJ = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRC))
BIN = $(BUILDDIR)/notes-light

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean
