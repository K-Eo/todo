#include <stdio.h>
#include <stdlib.h>

typedef void (*die_callback)(void);

void die(const char *string);
void on_die(die_callback callback);
