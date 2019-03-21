#include <ctype.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

void enable_raw_mode();
void disable_raw_mode();
int get_window_size(int *rows, int *cols);
int get_cursor_position(int *rows, int *cols);
