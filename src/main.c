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

enum keys {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum modes {
    M_NORMAL,
    M_INSERT
};

struct Config {
    int cx, cy;
    int rowoff;
    int screenrows;
    int screencols;
    int numtodos;
    int mode;
    Todo *todo;
};

struct Config state;

void clear_screen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    write(STDOUT_FILENO, "\x1b[?25h", 6);
}

int read_key() {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

void move_cursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (state.cx != 0 && state.mode == M_INSERT) {
                state.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (state.cx != state.screencols - 1 && state.mode == M_INSERT) {
                state.cx++;
            }
            break;
        case ARROW_UP:
            if (state.cy != 0) {
                state.cy--;
            }
            break;
        case ARROW_DOWN:
            if (state.cy < state.numtodos - 1) {
                state.cy++;
            }
            break;
    }
}

void toggle_todo() {
    int *done = &state.todo[state.cy].done;
    *done = *done == 0 ? 1 : 0;
}

void process_keys() {
    int c = read_key();

    switch (c) {
        case ctrl_key('q'):
            clear_screen();
            exit(0);
            break;

        case ' ':
            if (state.mode == M_NORMAL) {
                toggle_todo();
            }
            break;

        case HOME_KEY:
            state.cy = 0;
            break;
        case END_KEY:
            state.cy = state.numtodos - 1;
            break;

        case PAGE_DOWN:
        case PAGE_UP:
            {
                int times = state.screenrows / 2;
                while (times--)
                    move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
            move_cursor(c);
            break;
    }
}

void scrolling() {
    if (state.cy < state.rowoff) {
        state.rowoff = state.cy;
    }

    if (state.cy >= state.rowoff + state.screenrows) {
        state.rowoff = state.cy - state.screenrows + 1;
    }
}

void renderTodo(struct buffer *content, struct Todo todo, int index) {
    buffer_append(content, "   ", 3);

    char pointer = index == state.cy ? '>' : ' ';
    buffer_append(content, &pointer, 1);
    buffer_append(content, " - ", 3);

    int length = todo.size;

    if (length + 7 > state.screencols)
        length = state.screencols;

    char *c = &todo.string[0];

    int i;
    for (i = 0; i < length; i++) {
        if (isalpha(c[i]) && todo.done) {
            buffer_append(content, "\x1b[9m", 5);
            buffer_append(content, "\x1b[35m", 5);
            buffer_append(content, &c[i], 1);
            buffer_append(content, "\x1b[30m", 5);
            buffer_append(content, "\x1b[0m", 5);
        } else {
            buffer_append(content, &c[i], 1);
        }
    }
}

void render(struct buffer *content) {
    for (int i = 0; i < state.screenrows; i++) {
        int filerow = i + state.rowoff;

        if (filerow >= state.numtodos) {
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
            renderTodo(content, state.todo[filerow], filerow);
        }

        buffer_append(content, "\x1b[K", 3);
        if (i < state.screenrows - 1) {
            buffer_append(content, "\r\n", 2);
        }
    }
}

void refresh_screen() {
    scrolling();

    struct buffer content = BUFFER_INIT;

    buffer_append(&content, "\x1b[?25l", 6);
    buffer_append(&content, "\x1b[H", 3);

    render(&content);

    char buffer[32];
    int y = (state.cy - state.rowoff) + 1;
    int x = state.cx + 1;

    snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", y, x);
    buffer_append(&content, buffer, strlen(buffer));

    if (state.mode == M_INSERT) {
        buffer_append(&content, "\x1b[?25h", 6);
    }

    write(STDOUT_FILENO, content.string, content.length);
    buffer_free(&content);
}

void push_todo(char *string, size_t length) {
    state.todo = realloc(state.todo, sizeof(Todo) * (state.numtodos + 1));

    int at = state.numtodos;
    state.todo[at].done = 0;
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
    state.cx = 3;
    state.cy = 0;
    state.numtodos = 0;
    state.todo = NULL;
    state.rowoff = 0;
    state.mode = M_NORMAL;

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
        refresh_screen();
        process_keys();
    }

    return 0;
}
