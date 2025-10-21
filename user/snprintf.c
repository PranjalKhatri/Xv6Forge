#include <stdarg.h>
#include "kernel/types.h"
#include "user.h"

void putint(char *buf, int *pos, int d) {
  int divisor = 1000000000;
  int started = 0;
  
  // Handle negative numbers
  if (d < 0) {
    buf[(*pos)++] = '-';
    d = -d;
  }
  
  // Extract each digit
  for (; divisor >= 1; divisor /= 10) {
    int digit = d / divisor;
    d %= divisor;
    
    if (digit != 0) started = 1;
    if (started)
      buf[(*pos)++] = '0' + digit;
  }
  
  // Handle zero
  if (!started)
    buf[(*pos)++] = '0';
}

void snprintf(char *buf, char *fmt, ...) {
  va_list args;
  int i = 0;
  int j = 0;
  char *s;
  
  va_start(args, fmt);
  
  for (i = 0; fmt[i] != 0;) {
    if (fmt[i] == '%') {
      i++;
      if (fmt[i] == 0) break;
      
      switch (fmt[i]) {
        case 'd':
          putint(buf, &j, va_arg(args, int));
          break;
        case 's':
          if ((s = va_arg(args, char*)) == 0)
            s = "(null)";
          for (; *s; s++)
            buf[j++] = *s;
          break;
        case '%':
          buf[j++] = '%';
          break;
        default:
          buf[j++] = '%';
          buf[j++] = fmt[i];
          break;
      }
      i++;
    } else {
      buf[j++] = fmt[i++];
    }
  }
  buf[j] = '\0';
  va_end(args);
}