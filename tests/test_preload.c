#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *text = malloc(32);
    assert(text != NULL);
    strcpy(text, "preload works");

    text = realloc(text, 128);
    assert(text != NULL);
    assert(strcmp(text, "preload works") == 0);

    int *values = calloc(16, sizeof(int));
    assert(values != NULL);
    for (int i = 0; i < 16; i++) {
        assert(values[i] == 0);
    }

    free(values);
    free(text);
    return 0;
}
