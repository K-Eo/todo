#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "terminal.h"
#include "dirty.h"

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

void render() {
    for (int i = 0; i < state.screenrows; i++) {
        write(STDOUT_FILENO, "~", 3);

        if (i < state.screenrows - 1) {
            write(STDOUT_FILENO, "\r\n", 2);
        }
    }
}

void refreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    render();

    write(STDOUT_FILENO, "\x1b[H", 3);
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
