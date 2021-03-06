#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
    TAB_KEY = 9,
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    ALT_ENTER,
    SHIFT_TAB
};

enum work_modes {
    WM_NORMAL,
    WM_INSERT
};

enum insertion_modes {
    IM_AFTER,
    IM_BEFORE,
    IM_CURRENT
};

struct cursor_state {
    int x;
    int y;
};

struct todos_stats {
    int count;
    int done;
    int todo;
};

struct config_state {
    struct cursor_state cursor;
    struct todos_stats stats;
    int row_offset;
    int screen_rows;
    int screen_cols;
    int work_mode;
    int insertion_mode;
    char status_message[80];
    time_t status_message_time;
    todo *todos;
    char *filename;
};

struct config_state state;

void set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(state.status_message, sizeof(state.status_message), fmt, ap);
    va_end(ap);
    state.status_message_time = time(NULL);
}

char *todos_to_string(int *buffer_length) {
    int total_length = 0;
    int i;

    for (i = 0; i < state.stats.count; i++) {
        total_length += state.todos[i].size + 3;
    }

    *buffer_length = total_length;

    char *buffer = malloc(total_length);
    char *p = buffer;

    for (i = 0; i < state.stats.count; i++) {
        *p = state.todos[i].done ? '-' : ' ';
        p++;
        *p = ' ';
        p++;
        memcpy(p, state.todos[i].string, state.todos[i].size);
        p += state.todos[i].size;
        *p = '\n';
        p++;
    }

    return buffer;
}

void when_save() {
    if (state.filename == NULL) return;

    int length;
    char *buffer = todos_to_string(&length);

    int file = open(state.filename, O_RDWR | O_CREAT, 0644);

    if (file != -1) {
        if (ftruncate(file, length) != 1) {
            if (write(file, buffer, length) == length) {
                close(file);
                free(buffer);
                set_status_message("%d bytes written to disk", length);
                return;
            }
        }

        close(file);
    }

    free(buffer);
    set_status_message("Can't save I/O error: %s", strerror(errno));
}

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

        if (seq[0] == '\r') {
            return ALT_ENTER;
        }

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
                    case 'Z': return SHIFT_TAB;
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
    todo *current = (state.cursor.y >= state.stats.count) ?
                    NULL : &state.todos[state.cursor.y];

    switch (key) {
        case ARROW_LEFT:
            if (state.cursor.x > TODO_OFFSET &&
                state.work_mode == WM_INSERT)
                state.cursor.x--;
            break;
        case ARROW_RIGHT:
            if (current && state.work_mode == WM_INSERT &&
                state.cursor.x < current->size + TODO_OFFSET)
                state.cursor.x++;
            break;
        case SHIFT_TAB:
        case ARROW_UP:
            if (state.cursor.y != 0 && state.work_mode == WM_NORMAL)
                state.cursor.y--;
            break;
        case TAB_KEY:
        case ARROW_DOWN:
            if (state.cursor.y < state.stats.count - 1 &&
                state.work_mode == WM_NORMAL)
                state.cursor.y++;
            break;
    }
}

void begin_insert_mode() {
    state.work_mode = WM_INSERT;
}

void end_insert_mode() {
    state.work_mode = WM_NORMAL;
}

void todo_del_char(todo *src, int at) {
    if (at < 0 || at >= src->size) return;
    memmove(&src->string[at], &src->string[at + 1], src->size - at);
    src->size--;
}

void del_char() {
    if (state.cursor.y == state.stats.count) return;

    todo *current = &state.todos[state.cursor.y];

    if (state.cursor.x > TODO_OFFSET) {
        todo_del_char(current, state.cursor.x - TODO_OFFSET - 1);
        state.cursor.x--;
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
    todo_insert_char(&state.todos[state.cursor.y], state.cursor.x - TODO_OFFSET, c);
    state.cursor.x++;
}

void push_todo(int at, char *string, size_t length, int done) {
    if (at < 0 || at > state.stats.count) return;

    state.todos = realloc(state.todos, sizeof(todo) * (state.stats.count + 1));
    memmove(&state.todos[at + 1], &state.todos[at], sizeof(todo) * (state.stats.count - at));

    state.todos[at].done = done;
    state.todos[at].size = length;
    state.todos[at].string = malloc(length + 1);
    memcpy(state.todos[at].string, string, length);
    state.todos[at].string[length] = '\0';

    state.stats.count++;

    if (done) {
        state.stats.done++;
    } else {
        state.stats.todo++;
    }
}

void toggle_todo() {
    int *done = &state.todos[state.cursor.y].done;
    *done = *done == 0 ? 1 : 0;

    if (*done) {
        state.stats.done++;
        state.stats.todo--;
    } else {
        state.stats.done--;
        state.stats.todo++;
    }
}

void free_todo(todo *src) {
    free(src->string);
}

void create_todo() {
    int at = state.insertion_mode == IM_AFTER ?
        state.cursor.y + 1 : state.cursor.y;

    if (at > state.stats.count) {
        at = state.stats.count;
    }

    push_todo(at, "", 0, 0);

    if (state.insertion_mode == IM_AFTER) {
        if (state.stats.count > state.cursor.y + 1) {
            state.cursor.y++;
        }
    }

    state.cursor.x = TODO_OFFSET;
}

void remove_todo(int at) {
    if (at < 0 || at >= state.stats.count) return;

    int done = state.todos[at].done;

    free_todo(&state.todos[at]);
    memmove(&state.todos[at], &state.todos[at + 1],
        sizeof(todo) * (state.stats.count - at - 1));

    state.stats.count--;

    if (done) {
        state.stats.done--;
    } else {
        state.stats.todo--;
    }
}

void edit_todo() {
    state.insertion_mode = IM_CURRENT;
    state.cursor.x = state.todos[state.cursor.y].size + TODO_OFFSET;
}

void normal_keys(int c) {
    switch (c) {
        case '\r':
            state.insertion_mode = IM_AFTER;
            create_todo();
            begin_insert_mode();
            break;

        case ALT_ENTER:
            state.insertion_mode = IM_BEFORE;
            create_todo();
            begin_insert_mode();
            break;

        case ctrl_key('q'):
            clear_screen();
            exit(0);
            break;

        case 'e':
            edit_todo();
            begin_insert_mode();
            break;

        case ' ':
            toggle_todo();
            when_save();
            break;

        case HOME_KEY:
            state.cursor.y = 0;
            break;
        case END_KEY:
            state.cursor.y = state.stats.count - 1;
            break;

        case DEL_KEY:
        case BACKSPACE:
            if (c == BACKSPACE) move_cursor(ARROW_UP);
            remove_todo(state.cursor.y);
            when_save();
            break;

        case PAGE_DOWN:
        case PAGE_UP:
            {
                int times = state.screen_rows / 2;
                while (times--)
                    move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case TAB_KEY:
        case SHIFT_TAB:
            move_cursor(c);
            break;
    }
}

void insert_keys(int c) {
    switch (c) {
        case '\x1b':
        case '\r':
            end_insert_mode();
            if (state.todos[state.cursor.y].size == 0) {
                remove_todo(state.cursor.y);
                if (state.insertion_mode == IM_AFTER) {
                    move_cursor(ARROW_UP);
                }
            } else {
                when_save();
            }
            break;

        case TAB_KEY:
            if (state.todos[state.cursor.y].size != 0) {
                state.insertion_mode = IM_AFTER;
                create_todo();
                begin_insert_mode();
            }
            break;

        case SHIFT_TAB:
            if (state.todos[state.cursor.y].size != 0) {
                state.insertion_mode = IM_BEFORE;
                create_todo();
                begin_insert_mode();
            }
            break;

        case PAGE_DOWN:
        case PAGE_UP:
        case ctrl_key('l'):
        case ALT_ENTER:
            break;

        case HOME_KEY:
            state.cursor.x = TODO_OFFSET;
            break;
        case END_KEY:
            if (state.cursor.y < state.stats.count) {
                state.cursor.x = state.todos[state.cursor.y].size + TODO_OFFSET;
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

    if (state.work_mode == WM_NORMAL) {
        normal_keys(c);
    } else {
        insert_keys(c);
    }
}

void scrolling() {
    if (state.cursor.y < state.row_offset) {
        state.row_offset = state.cursor.y;
    }

    if (state.cursor.y >= state.row_offset + state.screen_rows) {
        state.row_offset = state.cursor.y - state.screen_rows + 1;
    }
}

void render_todo(struct buffer *content, struct todo src, int index) {
    buffer_append(content, "  ", 2);

    char pointer = index != state.cursor.y ? ' ' :
                    state.work_mode == WM_NORMAL ? '>' : '*';

    buffer_append(content, &pointer, 1);
    buffer_append(content, " ", 1);
    buffer_append(content, src.done ? " " : "-", 1);
    buffer_append(content, " ", 1);

    int length = src.size;

    if (length + TODO_OFFSET > state.screen_cols)
        length = state.screen_cols;

    char *c = &src.string[0];

    int i;
    for (i = 0; i < length; i++) {
        if (isprint(c[i]) && src.done) {
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
    for (int i = 0; i < state.screen_rows; i++) {
        int filerow = i + state.row_offset;

        if (filerow >= state.stats.count) {
            if (state.stats.count == 0 && i == 0) {
                char welcome[80];
                int welcome_length = snprintf(welcome, sizeof(welcome),
                    "Todo App -- version %s", TODO_VERSION);

                if (welcome_length > state.screen_cols)
                    welcome_length = state.screen_cols;

                int padding = (state.screen_cols - welcome_length) / 2;

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

    for (i = 0; i < state.stats.count; i++) {
        if (state.todos[i].done) {
            (*done)++;
        } else {
            (*todo)++;
        }
    }
}

void render_status_bar(struct buffer *dest) {
    buffer_append(dest, "\x1b[7m", 4);

    int done = 0;
    int todo = 0;

    get_stats(&done, &todo);

    char status[80];
    int length = snprintf(status, sizeof(status), "%2d - %2d/%2d/%2d",
        state.cursor.y + 1, state.stats.todo, state.stats.done, state.stats.count);

    if (length > state.screen_cols) length = state.screen_cols;

    buffer_append(dest, status, length);

    while (length < state.screen_cols) {
        buffer_append(dest, " ", 1);
        length++;
    }

    buffer_append(dest, "\x1b[m", 3);
    buffer_append(dest, "\r\n", 2);
}

void render_status_message(struct buffer *dest) {
    buffer_append(dest, "\x1b[K", 3);

    int message_length = strlen(state.status_message);

    if (message_length > state.screen_cols)
        message_length = state.screen_cols;

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
    int y = (state.cursor.y - state.row_offset) + 1;
    int x = state.cursor.x + 1;

    snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", y, x);
    buffer_append(&content, buffer, strlen(buffer));

    if (state.work_mode == WM_INSERT) {
        buffer_append(&content, "\x1b[?25h", 6);
    }

    write(STDOUT_FILENO, content.string, content.length);
    buffer_free(&content);
}

void when_open(char *filename) {
    free(state.filename);
    state.filename = strdup(filename);

    FILE *file = fopen(filename, "a+");
    if (!file) die("fopen");

    char *line = NULL;
    size_t lines_captured = 0;
    ssize_t line_length;

    while((line_length = getline(&line, &lines_captured, file)) != -1) {
        while (line_length > 0 &&
            (line[line_length - 1] == '\n' || line[line_length - 1] == '\r')) {
                line_length--;
            }

        if (line_length > 2 && line[1] == ' ' &&
            (line[0] == ' ' || line[0] == '-')) {
            int done = line[0] == '-' ? 1 : 0;
            push_todo(state.stats.count, &line[2], line_length - 2, done);
        } else {
            continue;
        }
    }

    free(line);
    fclose(file);

}

void init() {
    state.cursor.x = 3;
    state.cursor.y = 0;
    state.stats.count = 0;
    state.todos = NULL;
    state.row_offset = 0;
    state.work_mode = WM_NORMAL;
    state.status_message[0] = '\0';
    state.status_message_time = 0;
    state.insertion_mode = IM_AFTER;

    if (get_window_size(&state.screen_rows, &state.screen_cols) == -1) {
        die("get_window_size");
    }

    state.screen_rows -= 2;
}

char *get_default_filename() {
    char *home_dir = getenv("HOME");

    if (home_dir) {
        char *default_filename = "/.todos.txt";

        int home_dir_length = strlen(home_dir);
        int default_filename_length = strlen(default_filename);
        int length = home_dir_length + default_filename_length + 1;

        char *filename = malloc(length);

        strcpy(filename, home_dir);
        strcpy(&filename[home_dir_length], default_filename);
        filename[length] = '\0';

        return filename;
    } else {
        return NULL;
    }
}

int main(int argc, char *argv[]) {
    enable_raw_mode();
    on_die(clear_screen);
    init();

    if (argc >= 2) {
        when_open(argv[1]);
    } else {
        char *filename = get_default_filename();

        if (filename) {
            when_open(filename);
            free(filename);
        }
    }

    set_status_message("OK");

    while (1) {
        refresh_screen();
        process_keys();
    }

    return 0;
}
