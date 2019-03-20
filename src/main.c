#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "terminal.h"
#include "dirty.h"
#include "buffer.h"
#include "todo.h"

#define TODO_VERSION "0.0.1"
#define ctrl_key(k) ((k) & 0x1f)

struct Config {
    int screenrows;
    int screencols;
    int numtodos;
    Todo *todo;
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
        if (i >= state.numtodos) {
            if (state.numtodos == 0 && i == 0) {
                char welcome[80];
                int welcome_length = snprintf(welcome, sizeof(welcome),
                    "Todo App -- version %s", TODO_VERSION);

                if (welcome_length > state.screencols)
                    welcome_length = state.screencols;

                int padding = (state.screencols - welcome_length) / 2;

                if (padding) {
                    buffer_append(content, "~", 1);
                    padding--;
                }

                while (padding--) buffer_append(content, " ", 1);

                buffer_append(content, welcome, welcome_length);
            } else {
                buffer_append(content, "~", 1);
            }
        } else {
            int length = state.todo[i].size;

            if (length > state.screencols)
                length = state.screencols;

            buffer_append(content, state.todo[i].string, length);
        }

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

void push_todo(char *string, size_t length) {
    state.todo = realloc(state.todo, sizeof(Todo) * (state.numtodos + 1));

    int at = state.numtodos;
    state.todo[at].size = length;
    state.todo[at].string = malloc(length + 1);
    memcpy(state.todo[at].string, string, length);
    state.todo[at].string[length] = '\0';
    state.numtodos++;
}

void when_open(char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) die("fopen");

    char *line = NULL;
    size_t lines_captured = 0;
    ssize_t line_length;

    while((line_length = getline(&line, &lines_captured, file)) != -1) {
        while (line_length > 0 &&
            (line[line_length - 1] == '\n' || line[line_length - 1] == '\r')) {
                line_length--;
            }

        push_todo(line, line_length);
    }

    free(line);
    fclose(file);

}

void init() {
    state.numtodos = 0;
    state.todo = NULL;

    if (getWindowSize(&state.screenrows, &state.screencols) == -1) {
        die("getWindowSize");
    }
}

int main(int argc, char *argv[]) {
    enableRawMode();
    on_die(clear_screen);
    init();

    if (argc >= 2) {
        when_open(argv[1]);
    }

    while (1) {
        refreshScreen();
        processKeys();
    }

    return 0;
}
