// console.h
#ifndef CONSOLE_H
#define CONSOLE_H

struct cons_state{
  uint mode;
  uint32 flags;
};

#define CONS_BUFFERED 0
#define CONS_RAW      1

#define CONS_FLG_ECHO 1

#endif
