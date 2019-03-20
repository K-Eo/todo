#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "terminal.h"
#include "dirty.h"
#include "buffer.h"

#define ctrl_key(k) ((k) & 0x1f)

struct Config {
    int screenrows;
    int screencols;
};

struct Config state;

void clear_screen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

char readKey() {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    return c;
}

void processKeys() {
    char c = readKey();

    switch (c) {
        case ctrl_key('q'):
            clear_screen();
            exit(0);
            break;
    }
}

void render(struct buffer *content) {
    for (int i = 0; i < state.screenrows; i++) {
        buffer_append(content, "~", 1);

        buffer_append(content, "\x1b[K", 3);
        if (i < state.screenrows - 1) {
            buffer_append(content, "\r\n", 2);
        }
    }
}

void refreshScreen() {
    struct buffer content = BUFFER_INIT;

    buffer_append(&content, "\x1b[?25l", 6);
    buffer_append(&content, "\x1b[H", 3);

    render(&content);

    buffer_append(&content, "\x1b[H", 3);
    buffer_append(&content, "\x1b[?25h", 6);

    write(STDOUT_FILENO, content.string, content.length);
    buffer_free(&content);
}

void init() {
    if (getWindowSize(&state.screenrows, &state.screencols) == -1) {
        die("getWindowSize");
    }
}

int main() {
    enableRawMode();
    on_die(clear_screen);
    init();

    while (1) {
        refreshScreen();
        processKeys();
    }

    return 0;
}
