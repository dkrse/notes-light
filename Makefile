CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -O2 $(shell pkg-config --cflags libadwaita-1 gtksourceview-5)
LDFLAGS = $(shell pkg-config --libs libadwaita-1 gtksourceview-5)

BUILDDIR = build
SRC = src/main.c src/window.c src/settings.c src/ssh.c \
      src/theme.c src/editor_view.c src/search.c src/ssh_window.c \
      src/actions.c src/actions_file.c src/actions_view.c src/actions_ssh.c
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
