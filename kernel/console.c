//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "console.h"

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

enum {
    ESC_NONE,      // normal input
    ESC_SEEN,      // saw ESC (27)
    ESC_BRACKET   // saw ESC then '['
} esc_state = ESC_NONE;
const int ESC_CODE = 27;

struct {
  struct spinlock lock;
  struct cons_state state;
  // input
  #define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;

//
// user write()s to the console go here.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  char buf[32];
  int i = 0;

  while(i < n){
    int nn = sizeof(buf);
    if(nn > n - i)
      nn = n - i;
    if(either_copyin(buf, user_src, src+i, nn) == -1)
      break;
    uartwrite(buf, nn);
    i += nn;
  }

  return i;
}

// Check if read timeout has expired
static int
check_timeout(void)
{
  if(cons.state.vtime == 0)
    return 0;
  
  if(ticks - cons.state.last_read_tick >= cons.state.vtime) {
    return 1;
  }
  return 0;
}

void
cons_timer_tick(void)
{
  acquire(&cons.lock);
  if(check_timeout() && cons.r == cons.w) {
    // printf("wakeup\n");
    // Timeout expired and no data waiting
    wakeup(&cons.r);
  }
  release(&cons.lock);
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){

      if(cons.state.vmin == 0 && cons.state.vtime > 0) {
        // VMIN=0, VTIME>0: non-blocking with timeout
        if(check_timeout()) {
          release(&cons.lock);
          return target-n; //possibly 0 also
        }
      }
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];
    cons.state.last_read_tick = ticks;

    if((cons.state.flags&CONS_FLG_COOKED) &&c == C('D')){  // end-of-file
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(cons.state.vmin > 0 && target-n >= cons.state.vmin) {
      break;
    }
    if((cons.state.flags&CONS_FLG_COOKED) && c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  acquire(&cons.lock);

  int is_cooked   = cons.state.flags & CONS_FLG_COOKED;
  int echo_back   = cons.state.flags & CONS_FLG_ECHO;
  int proc_dump   = cons.state.flags & CONS_FLG_PROCDUMP;
  int is_raw      = cons.state.mode == CONS_RAW;
  int is_buffered = cons.state.mode == CONS_BUFFERED;

  switch(c){
  case C('P'):  // Print process list.
    if(proc_dump){
      procdump();
    }else if (is_raw) {
      // In raw mode with procdump disabled, treat it as normal input
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
      cons.w = cons.e;
      wakeup(&cons.r);
    }
    break;
  case C('U'):  // Kill line.
    if(is_cooked){
      while(cons.e != cons.w &&
            cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
        cons.e--;
        consputc(BACKSPACE);
      }
      break;
    }else{
      // fall through to default //TODO: change this design of falling through to more robust one
    }
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c;
      //user handles backspace in raw mode.
      if(is_raw){
        if(esc_state == ESC_NONE && c == ESC_CODE){
          esc_state = ESC_SEEN;
        }else if(esc_state == ESC_SEEN && c == '['){
          esc_state = ESC_BRACKET;
        }else if(esc_state == ESC_BRACKET){
          esc_state = ESC_NONE;
        }else{
          if(echo_back){
            consputc(c);
          }
        }
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
        cons.w = cons.e;
        wakeup(&cons.r);
      }else if(is_buffered){
        if(c == C('H') || c == '\x7f'){
          if(cons.e != cons.w){
            cons.e--;
            consputc(BACKSPACE);
          }
          break;
        }
        // echo back to the user.
        if(echo_back)
          consputc(c);

        // store for consumption by consoleread().
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

        if(c == '\n' || (is_cooked && c == C('D')) || cons.e-cons.r == INPUT_BUF_SIZE){
          // wake up consoleread() if a whole line (or end-of-file)
          // has arrived.
          cons.w = cons.e;
          wakeup(&cons.r);
        }
      }
    }
      break;
  }
  
  release(&cons.lock);
}
void
setconsMode(int md){
  acquire(&cons.lock);
  if(md == CONS_RAW){
    cons.w=cons.e;
    wakeup(&cons.r);
    cons.state.mode = CONS_RAW;
  }else{
    cons.state.mode = CONS_BUFFERED;
  }
  release(&cons.lock);
}
void
ConsSetFlag(int flg, int set){
  set = set ? 1 : 0;
  acquire(&cons.lock);
  if(set)
    cons.state.flags |= flg;
  else
    cons.state.flags &=  ~flg;
  release(&cons.lock);
}

void GetConsState(struct cons_state *state){
  acquire(&cons.lock);
  memmove(state,&cons.state,sizeof(cons.state));
  release(&cons.lock);
}
void SetConsState(struct cons_state* state){
  acquire(&cons.lock);
  memmove(&cons.state,state,sizeof(cons.state));
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();
  cons.state.flags |= ~0;
  cons.state.vmin   = 1e6; // large number for buffered
  cons.state.vtime = 0;
  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
