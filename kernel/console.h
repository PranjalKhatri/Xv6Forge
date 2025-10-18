// console.h
#ifndef CONSOLE_H
#define CONSOLE_H

struct cons_state{
  uint mode;
  uint32 flags;
  // VMIN/VTIME support
  uint vmin;           //minimum characters to return, (0 = return on timeout).
  uint vtime;          //Timeoutin ticks.
  uint last_read_tick; //When last character was typed.
};

#define CONS_BUFFERED 0
#define CONS_RAW      1

#define CONS_FLG_ECHO     (1 << 0)  // Enable echoing of typed characters
#define CONS_FLG_COOKED   (1 << 1)  // Enable line discipline (Ctrl-D, Ctrl-U, etc.)
#define CONS_FLG_SIGINT   (1 << 2)  // Generate signal on Ctrl-C (future use)
#define CONS_FLG_CTRL     (1 << 3)  // Interpret control chars (backspace, etc.)
#define CONS_FLG_PROCDUMP (1 << 4)  // Enable Ctrl-P process dump


#endif
