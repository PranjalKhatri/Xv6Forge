/*** includes ***/

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/console.h"

/*** defines  ***/
#define CTRL_KEY(k) ((k) & 0x1f)

typedef enum
{
  false = 0,
  true = 1
} bool;

/*** data ***/
struct editorConfig {
  int rows;
  int columns;
};
struct editorConfig E;
struct cons_state cs_initial, cs_raw;

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

/*** output ***/
void editorDrawRows() {
  int y;
  for (y = 0; y < E.rows; y++) {
    write(STDOUT, "~\r\n", 3);
  }
}

void editorRefreshScreen() {
  write(STDOUT, "\x1b[2J", 4);
  write(STDOUT, "\x1b[H", 3);
  editorDrawRows();
  write(STDOUT, "\x1b[H", 3);
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
  SetConsState(&cs_initial);
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
  if (GetConsState(&cs_initial) < 0)
    die("Unable to get initial consState\n");

  cs_raw = cs_initial;
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