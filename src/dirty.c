#include "dirty.h"

die_callback on_die_callback;

void die(const char *string) {
    perror(string);

    if (on_die_callback) {
        on_die_callback();
    }

    exit(1);
}

void on_die(die_callback callback) {
    on_die_callback = callback;
}
