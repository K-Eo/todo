#include <stdlib.h>
#include <string.h>

#define BUFFER_INIT { NULL, 0 }

struct buffer {
    char *string;
    int length;
};

void buffer_append(struct buffer *dest, const char *string, int length);
void buffer_free(struct buffer *target);
