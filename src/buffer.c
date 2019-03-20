#include "buffer.h"

void buffer_append(struct buffer *dest, const char *string, int length) {
    char *new = realloc(dest->string, dest->length + length);

    if (new == NULL) return;

    memcpy(&new[dest->length], string, length);
    dest->string = new;
    dest->length += length;
}

void buffer_free(struct buffer *target) {
    free(target->string);
}
