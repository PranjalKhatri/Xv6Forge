#include "kernel/types.h"
#include "user/user.h"
#include "kernel/procinfo.h"

#define MAX_PROCS 64
#define INTERVAL 100 // refresh every 100 ticks

struct cpu_prev
{
    int pid;
    uint64 prev_ticks;
};
static const char *states[] = {
    [0] "unused  ",
    [1] "embryo  ",
    [2] "sleeping",
    [3] "runnable",
    [4] "running ",
    [5] "zombie  "};
int main()
{
    struct procinfo p_table[MAX_PROCS];
    struct cpu_prev prev[MAX_PROCS];
    int num_procs;
    int first_run = 1;

    while (1)
    {
        num_procs = getprocinfo(p_table);
        if (num_procs < 0)
        {
            fprintf(2, "top: getprocinfo failed\n");
            exit(1);
        }

        // Clear screen
        printf("\033[2J\033[H");
        printf("PID\tPPID\tSTATE\t\tSIZE\tNAME\tCPU\n");

        for (int i = 0; i < num_procs; i++)
        {
            uint64 cpu_diff = 0;
            if (!first_run)
            {
                for (int j = 0; j < MAX_PROCS; j++)
                {
                    if (prev[j].pid == p_table[i].pid)
                    {
                        cpu_diff = p_table[i].cputicks - prev[j].prev_ticks;
                        prev[j].prev_ticks = p_table[i].cputicks;
                        break;
                    }
                }
            }
            else
            {
                prev[i].pid = p_table[i].pid;
                prev[i].prev_ticks = p_table[i].cputicks;
            }

            printf("%d\t%d\t%s\t%d\t%s\t%ld\n",
                   p_table[i].pid,
                   p_table[i].ppid,
                   states[p_table[i].state],
                   p_table[i].sz,
                   p_table[i].name,
                   cpu_diff);
        }

        first_run = 0;

        pause(INTERVAL);
    }

    exit(0);
}
