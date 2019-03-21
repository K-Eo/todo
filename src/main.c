#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "terminal.h"
#include "support.h"
#include "buffer.h"
#include "todo.h"

#define TODO_VERSION "0.0.1"
#define TODO_OFFSET 6
#define ctrl_key(k) ((k) & 0x1f)

enum keys {
    BACKSPACE = 127,
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

struct config_state {
    int cx, cy;
    int row_offset;
    int screenrows;
    int screencols;
    int numtodos;
    int mode;
    char status_message[80];
    time_t status_message_time;
    todo *todos;
};

struct config_state state;

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
    todo *current = (state.cy >= state.numtodos) ? NULL : &state.todos[state.cy];

    switch (key) {
        case ARROW_LEFT:
            if (state.cx > TODO_OFFSET && state.mode == M_INSERT) {
                state.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (current && state.mode == M_INSERT && state.cx < current->size + TODO_OFFSET) {
                state.cx++;
            }
            break;
        case ARROW_UP:
            if (state.cy != 0 && state.mode == M_NORMAL) {
                state.cy--;
            }
            break;
        case ARROW_DOWN:
            if (state.cy < state.numtodos - 1 && state.mode == M_NORMAL) {
                state.cy++;
            }
            break;
    }
}

void begin_insert_mode() {
    state.mode = M_INSERT;
}

void end_insert_mode() {
    state.mode = M_NORMAL;
}

void todo_del_char(todo *src, int at) {
    if (at < 0 || at >= src->size) return;
    memmove(&src->string[at], &src->string[at + 1], src->size - at);
    src->size--;
}

void del_char() {
    if (state.cy == state.numtodos) return;

    todo *current = &state.todos[state.cy];

    if (state.cx > TODO_OFFSET) {
        todo_del_char(current, state.cx - TODO_OFFSET - 1);
        state.cx--;
    }
}

void todo_insert_char(todo *dest, int at, int c) {
    if (at < 0 || at > dest->size) at = dest->size;
    dest->string = realloc(dest->string, dest->size + 2);
    memmove(&dest->string[at + 1], &dest->string[at], dest->size - at + 1);
    dest->size++;
    dest->string[at] = c;
}

void insert_char(int c) {
    todo_insert_char(&state.todos[state.cy], state.cx - TODO_OFFSET, c);
    state.cx++;
}

void push_todo(int at, char *string, size_t length) {
    if (at < 0 || at > state.numtodos) return;

    state.todos = realloc(state.todos, sizeof(todo) * (state.numtodos + 1));
    memmove(&state.todos[at + 1], &state.todos[at], sizeof(todo) * (state.numtodos - at));

    state.todos[at].done = 0;
    state.todos[at].size = length;
    state.todos[at].string = malloc(length + 1);
    memcpy(state.todos[at].string, string, length);
    state.todos[at].string[length] = '\0';
    state.numtodos++;
}

void toggle_todo() {
    int *done = &state.todos[state.cy].done;
    *done = *done == 0 ? 1 : 0;
}

void free_todo(todo *src) {
    free(src->string);
}

void create_todo() {
    push_todo(state.cy + 1, "", 0);
    state.cy++;
    state.cx = TODO_OFFSET;
}

void remove_todo(int at) {
    if (at < 0 || at >= state.numtodos) return;
    free_todo(&state.todos[at]);
    memmove(&state.todos[at], &state.todos[at + 1],
        sizeof(todo) * (state.numtodos - at - 1));
    state.numtodos--;
}

void normal_keys(int c) {
    switch (c) {
        case '\r':
            create_todo();
            begin_insert_mode();
            break;

        case ctrl_key('q'):
            clear_screen();
            exit(0);
            break;

        case ' ':
            toggle_todo();
            break;

        case HOME_KEY:
            state.cy = 0;
            break;
        case END_KEY:
            state.cy = state.numtodos - 1;
            break;

        case DEL_KEY:
        case BACKSPACE:
            remove_todo(state.cy);
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

void insert_keys(int c) {
    switch (c) {
        case '\r':
            end_insert_mode();
            break;
        case PAGE_DOWN:
        case PAGE_UP:
        case '\t':
        case '\x1b':
        case ctrl_key('l'):
            break;

        case HOME_KEY:
            state.cx = TODO_OFFSET;
            break;
        case END_KEY:
            if (state.cy < state.numtodos) {
                state.cx = state.todos[state.cy].size + TODO_OFFSET;
            }
            break;

        case BACKSPACE:
        case ctrl_key('h'):
        case DEL_KEY:
            if (c == DEL_KEY) move_cursor(ARROW_RIGHT);
            del_char();
            break;

        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
            move_cursor(c);
            break;
        default:
            insert_char(c);
    }
}

void process_keys() {
    int c = read_key();

    if (state.mode == M_NORMAL) {
        normal_keys(c);
    } else {
        insert_keys(c);
    }
}

void scrolling() {
    if (state.cy < state.row_offset) {
        state.row_offset = state.cy;
    }

    if (state.cy >= state.row_offset + state.screenrows) {
        state.row_offset = state.cy - state.screenrows + 1;
    }
}

void render_todo(struct buffer *content, struct todo src, int index) {
    buffer_append(content, "  ", 2);

    char pointer = index == state.cy ? '>' : ' ';
    buffer_append(content, &pointer, 1);
    buffer_append(content, " - ", 3);

    int length = src.size;

    if (length + TODO_OFFSET > state.screencols)
        length = state.screencols;

    char *c = &src.string[0];

    int i;
    for (i = 0; i < length; i++) {
        if (isalpha(c[i]) && src.done) {
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
        int filerow = i + state.row_offset;

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
            render_todo(content, state.todos[filerow], filerow);
        }

        buffer_append(content, "\x1b[K", 3);
        buffer_append(content, "\r\n", 2);
    }
}

void get_stats(int *done, int *todo) {
    int i;

    for (i = 0; i < state.numtodos; i++) {
        if (state.todos[i].done) {
            (*done)++;
        } else {
            (*todo)++;
        }
    }
}

void set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(state.status_message, sizeof(state.status_message), fmt, ap);
    va_end(ap);
    state.status_message_time = time(NULL);
}

void render_status_bar(struct buffer *dest) {
    buffer_append(dest, "\x1b[7m", 4);

    int done = 0;
    int todo = 0;

    get_stats(&done, &todo);

    char status[80];
    int length = snprintf(status, sizeof(status), "%d %d %d",
        todo, done, state.numtodos);

    if (length > state.screencols) length = state.screencols;

    buffer_append(dest, status, length);

    while (length < state.screencols) {
        buffer_append(dest, " ", 1);
        length++;
    }

    buffer_append(dest, "\x1b[m", 3);
    buffer_append(dest, "\r\n", 2);
}

void render_status_message(struct buffer *dest) {
    buffer_append(dest, "\x1b[K", 3);

    int message_length = strlen(state.status_message);

    if (message_length > state.screencols)
        message_length = state.screencols;

    if (message_length && time(NULL) - state.status_message_time < 5)
        buffer_append(dest, state.status_message, message_length);
}

void refresh_screen() {
    scrolling();

    struct buffer content = BUFFER_INIT;

    buffer_append(&content, "\x1b[?25l", 6);
    buffer_append(&content, "\x1b[H", 3);

    render(&content);
    render_status_bar(&content);
    render_status_message(&content);

    char buffer[32];
    int y = (state.cy - state.row_offset) + 1;
    int x = state.cx + 1;

    snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", y, x);
    buffer_append(&content, buffer, strlen(buffer));

    if (state.mode == M_INSERT) {
        buffer_append(&content, "\x1b[?25h", 6);
    }

    write(STDOUT_FILENO, content.string, content.length);
    buffer_free(&content);
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

        push_todo(state.numtodos, line, line_length);
    }

    free(line);
    fclose(file);

}

void init() {
    state.cx = 3;
    state.cy = 0;
    state.numtodos = 0;
    state.todos = NULL;
    state.row_offset = 0;
    state.mode = M_NORMAL;
    state.status_message[0] = '\0';
    state.status_message_time = 0;

    if (getWindowSize(&state.screenrows, &state.screencols) == -1) {
        die("getWindowSize");
    }

    state.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    on_die(clear_screen);
    init();

    if (argc >= 2) {
        when_open(argv[1]);
    }

    set_status_message("OK");

    while (1) {
        refresh_screen();
        process_keys();
    }

    return 0;
}
