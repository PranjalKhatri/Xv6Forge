#include "kernel/types.h"
#include "user/user.h"
#include "kernel/console.h"
#include "readline.h"

#define C(x) ((x)-'@')

static char history[HISTORY_SIZE][MAX_CMD];
static int cur = 0;
static char *err_commandsize = "command too long!";
static char buf[MAX_CMD];
static int c;
static int bufidx = 0;
static int cursorpos = 0;
static int hisindx = 0;

int setupConsole()
{
    setconsmode(CONS_RAW);
    ConsSetFlag(CONS_FLG_ECHO,1);
    ConsSetFlag(CONS_FLG_COOKED,1);
    ConsSetFlag(CONS_FLG_PROCDUMP,1);
    return 0;
}

void clear_line(int bufidx)
{
    while (bufidx--)
    {
        write(1, "\b \b", 3);
    }
}

int getLine(char *dest)
{
    while ((c = getch()))
    {
        if (c == KEY_UP)
        {
            if (cur == 0)
                continue;
            if (hisindx > 0)
                hisindx--;
            clear_line(bufidx);
            memset(buf, 0, sizeof(buf));
            strcpy(buf, history[hisindx]);
            bufidx = strlen(buf);
            cursorpos = bufidx;
            write(1, buf, bufidx);
        }
        else if (c == KEY_DOWN)
        {
            if (cur == 0)
                continue;
            if (hisindx < cur - 1)
                hisindx++;
            clear_line(bufidx);
            memset(buf, 0, sizeof(buf));
            strcpy(buf, history[hisindx]);
            bufidx = strlen(buf);
            cursorpos = bufidx;
            write(1, buf, bufidx);
        }
        else if (c == KEY_LEFT)
        {
            if (cursorpos > 0)
            {
                write(1, "\b", 1);
                cursorpos--;
            }
        }
        else if (c == KEY_RIGHT)
        {
            if (cursorpos < bufidx)
            {
                write(1, buf + cursorpos, 1);
                cursorpos++;
            }
        }
        else if (c == KEY_HOME)
        {
            while (cursorpos > 0)
            {
                write(1, "\b", 1);
                cursorpos--;
            }
        }
        else if (c == '\b' || c == 127)
        {
            if (cursorpos > 0)
            {
                cursorpos--;
                for (int i = cursorpos; i < bufidx; i++)
                {
                    buf[i] = buf[i + 1];
                }
                bufidx--;

                write(1, "\b", 1);
                write(1, buf + cursorpos, bufidx - cursorpos);
                write(1, " ", 1);
                for (int i = bufidx; i >= cursorpos; i--)
                {
                    write(1, "\b", 1);
                }
            }
        }
        else if (c == '\n')
        {
            buf[bufidx] = 0;
            if (bufidx > 0)
            {
                strcpy(history[cur % HISTORY_SIZE], buf);
                strcpy(dest,history[cur%HISTORY_SIZE]);
                cur++;
            }
            bufidx = 0;
            cursorpos = 0;
            hisindx = cur;
            memset(buf, 0, sizeof(buf));
            return 0;
        }
        else if (c == C('D'))
        {
            write(1, "\n", 1);
            return -1;
            // break;
        }
        else if (c >= 32 && c < 127)
        {
            if (bufidx < MAX_CMD - 1)
            {
                for (int i = bufidx; i > cursorpos; i--)
                {
                    buf[i] = buf[i - 1];
                }
                buf[cursorpos] = c;
                bufidx++;
                cursorpos++;

                if (cursorpos < bufidx)
                {
                    write(1, buf + cursorpos, bufidx - cursorpos);
                    for (int i = cursorpos; i < bufidx; i++)
                    {
                        write(1, "\b", 1);
                    }
                }
            }
            else
            {
                write(2, err_commandsize, strlen(err_commandsize));
                write(2, "\n", 1);
            }
        }else{
            write(1,"\b \b",4);//clear the unsupported character
            write(2,"\a",2);//beep
        }
    }

    return 0;
}
