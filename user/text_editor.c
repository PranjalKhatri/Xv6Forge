/*** includes ***/

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/console.h"
#include <stdbool.h>
/*** defines  ***/
#define PIM_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
#define NULL 0

/*** data ***/
struct editorConfig {
  int rows;
  int columns;
  struct cons_state cs_initial;
};
struct editorConfig E;

void get_window_size(int *rows, int *cols);
bool isCntrl(unsigned char c);

/*** cleanup and exit ***/
void resetConsole();
void die(const char *s);
void cleanupAndExit(int status);

/*** terminal ***/
void enableRawMode();

char editorReadKey()
{
  int nread;
  char c;
  while ((nread = read(STDIN, &c, 1)) != 1)
  {
  }
  return c;
}

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};
#define ABUF_INIT {NULL, 0}
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = malloc(ab->len+len);
  if (new == NULL) return;
  if(ab->b)
    memcpy(new,ab->b,ab->len);
  memcpy(&new[ab->len], s, len);
  if(ab->b)
    free(ab->b);
  ab->b = new;
  ab->len += len;
}
void abFree(struct abuf *ab) {
  if(ab->b)
    free(ab->b);
  ab->b = 0;
  ab->len = 0;
}

/*** output ***/
void
editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.rows; y++) {
    if (y == E.rows / 3) {
      char welcome[80] = "PIM editor -- version ";
      strcat(welcome, PIM_VERSION);

      int welcomelen = strlen(welcome);
      if (welcomelen > E.columns)
        welcomelen = E.columns;

      int padding = (E.columns - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }

      while (padding-- > 0)
        abAppend(ab, " ", 1);

      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }
    // clear to end of line
    abAppend(ab, "\x1b[K", 3);
    // move to next line (except after the last one)
    if (y < E.rows - 1)
      abAppend(ab, "\r\n", 2);
  }
}
void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;
  
  abAppend(&ab, "\x1b[?25l", 6);//hide cursor
  abAppend(&ab, "\x1b[H", 3);//move to home
  
  editorDrawRows(&ab);
  
  abAppend(&ab, "\x1b[?25h", 6);//show cursor
  abAppend(&ab, "\x1b[H", 3);
  
  write(STDOUT, ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/
void editorProcessKeypress()
{
  char c = editorReadKey();
  switch (c)
  {
  case CTRL_KEY('q'):
    cleanupAndExit(0);
    break;
  }
}

/*** init ***/
int main()
{
  enableRawMode();
  get_window_size(&E.rows,&E.columns);
  while(1){
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}

bool isCntrl(unsigned char c)
{
  return (c < 0x20) || (c == 0x7f);
}

void resetConsole(){
  SetConsState(&E.cs_initial);
}
void die(const char *s)
{
  write(STDOUT, "\x1b[2J", 4);
  write(STDOUT, "\x1b[H", 3);
  write(2, s, strlen(s));
  exit(1);
}
void cleanupAndExit(int status){
  write(STDOUT, "\x1b[2J", 4);
  write(STDOUT, "\x1b[H", 3);
  resetConsole();
  exit(status);
}
void enableRawMode()
{
  if (GetConsState(&E.cs_initial) < 0)
    die("Unable to get initial consState\n");

  struct cons_state cs_raw;
  cs_raw = E.cs_initial;
  cs_raw.mode = CONS_RAW;
  cs_raw.flags = 0;
  cs_raw.vmin = 0;
  cs_raw.vtime = 10;

  if (SetConsState(&cs_raw) < 0)
    die("Unable to set raw console mode\n");
}

void get_window_size(int *rows, int *cols) {
  write(STDOUT, "\x1b[s", 3);      // Save cursor
  write(STDOUT, "\x1b[999;999H", 10); // Move to bottom-right
  write(STDOUT, "\x1b[6n", 4);     // Request cursor position
  
  // Read response: ESC [ rows ; cols R
  char buf[32];
  int nread = read(STDIN, buf, sizeof(buf));
  
  *rows = 24;
  *cols = 80;
  
  // Parse "ESC [ rows ; cols R"
  if(nread > 0 && buf[0] == '\x1b' && buf[1] == '[') {
    int r = 0, c = 0;
    int i = 2;
    
    // Parse rows
    while(i < nread && buf[i] >= '0' && buf[i] <= '9') {
      r = r * 10 + (buf[i] - '0');
      i++;
    }
    
    // Skip semicolon
    if(i < nread && buf[i] == ';')
      i++;
    
    // Parse cols
    while(i < nread && buf[i] >= '0' && buf[i] <= '9') {
      c = c * 10 + (buf[i] - '0');
      i++;
    }
    
    if(r > 0 && c > 0) {
      *rows = r;
      *cols = c;
    }
  }
  
  write(STDOUT, "\x1b[u", 3);      // Restore cursor
}