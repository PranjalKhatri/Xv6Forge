#ifndef _READLINE_H
#define _READLINE_H

#define HISTORY_SIZE 20
#define MAX_CMD 100

int setupConsole();
void clear_line(int bufidx);
int history_getline(char *dest);

#endif