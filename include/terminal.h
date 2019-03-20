#include <ctype.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

void enableRawMode();
void disableRawMode();
int getWindowSize(int *rows, int *cols);
int getCursorPosition(int *rows, int *cols);
