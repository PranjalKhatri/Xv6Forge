#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void overflow_me(char *input, int len)
{
    char buffer[16]; // buffer on stack?
    memcpy(buffer, input, len);
    // print 100 bytes after buffer
    for (int i = 0; i < 100; i++)
    {
        printf("buffer+%d | loc: %p | dec: %d | char: %c\n",
               i, (buffer + i), *(buffer+i),
               (*(buffer+i) >= 32 && *(buffer+i) < 127) ? *(buffer+i) : '.');
    }
}

int main(int argc, char *argv[])
{
    char exploit_buffer[256];

    printf("Reading payload from standard input...\n");
    int n = read(0, exploit_buffer, sizeof(exploit_buffer));
    printf("Bytes read: %d\n", n);

    if (n > 0)
    {
        // We no longer add a null terminator. We pass the length directly.
        overflow_me(exploit_buffer, n);
    }

    printf("Program finished normally.\n");
    exit(0);
}