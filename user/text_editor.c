#include "kernel/types.h"
#include "user/user.h"
#include "kernel/console.h"

struct cons_state initial;

void die(const char *s) {
  write(2,s,sizeof(s));
  exit(1);
}

void enableRawMode(){
  GetConsState(&initial);
  setconsmode(CONS_RAW);
  ConsSetFlag(CONS_FLG_ECHO,0);
}

int main(){
  enableRawMode();
  char c;
  while (read(0,&c,1) && c != 'q');

  SetConsState(&initial);
}
