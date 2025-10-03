#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void
usage(void)
{
  printf("Usage: xargs [OPTIONS] command [args...]\n");
  printf("Options:\n");
  printf("  --help        Show this help message\n");
  printf("  --version     Show version info\n");
  printf("  -a FILE       Read input from FILE instead of stdin\n");
  printf("  -r            Do not run command if no input is read\n");
  printf("  -n NUM        Use at most NUM args per command line\n");
  printf("  -p            Prompt before each command\n");
  printf("  -P NUM        Run up to NUM processes in parallel\n");
}

int
main(int argc, char *argv[])
{
  int flag_r = 0, flag_p = 0;
  char *infile = 0;
  int argstart = 1;
  int maxargs = 0;   // 0 = unlimited args per exec
  int maxprocs = 1;  // default sequential
  int active = 0;    // active children count

  // Parse options
  for (int k = 1; k < argc; k++) {
    if (!strcmp(argv[k], "--help")) {
      usage();
      exit(0);
    } else if (!strcmp(argv[k], "--version")) {
      printf("xargs (xv6) 1.0\n");
      exit(0);
    } else if (!strcmp(argv[k], "-r")) {
      flag_r = 1;
    } else if (!strcmp(argv[k], "-p")) {
      flag_p = 1;
    } else if (!strcmp(argv[k], "-a")) {
      if (k+1 >= argc) {
        fprintf(2, "xargs: -a requires a file name\n");
        exit(1);
      }
      infile = argv[k+1];
      k++;
    } else if (!strcmp(argv[k], "-n")) {
      if (k+1 >= argc) {
        fprintf(2, "xargs: -n requires a number\n");
        exit(1);
      }
      maxargs = atoi(argv[k+1]);
      if (maxargs <= 0) {
        fprintf(2, "xargs: invalid -n value\n");
        exit(1);
      }
      k++;
    } else if (!strcmp(argv[k], "-P")) {
      if (k+1 >= argc) {
        fprintf(2, "xargs: -P requires a number\n");
        exit(1);
      }
      maxprocs = atoi(argv[k+1]);
      if (maxprocs <= 0) maxprocs = 1;
      k++;
    } else {
      argstart = k;
      break;
    }
  }

  if (argstart >= argc) {
    fprintf(2, "xargs: missing command\n");
    exit(1);
  }

  int fd = 0;
  if (infile) {
    fd = open(infile, O_RDONLY);
    if (fd < 0) {
      fprintf(2, "xargs: cannot open %s\n", infile);
      exit(1);
    }
  }

  char buf[512];
  int n = read(fd, buf, sizeof(buf));
  if (n < 0) {
    fprintf(2, "xargs: read error\n");
    exit(1);
  }

  // Tokenize
  char *tokens[128];
  int ntok = 0;
  int j = 0;
  while (j < n) {
    while (j < n && (buf[j] == ' ' || buf[j] == '\n' || buf[j] == '\t'))
      buf[j++] = 0;
    if (j >= n) break;
    tokens[ntok++] = &buf[j];
    while (j < n && buf[j] != ' ' && buf[j] != '\n' && buf[j] != '\t')
      j++;
  }

  if (flag_r && ntok == 0)
    exit(0);

  int baseargs = argc - argstart;
  int pos = 0;
  while (pos < ntok || (ntok == 0 && pos == 0)) {
    char *args[64];
    int i;
    for (i = 0; i < baseargs; i++)
      args[i] = argv[argstart + i];

    int count = 0;
    while (pos < ntok && (maxargs == 0 || count < maxargs)) {
      args[i++] = tokens[pos++];
      count++;
    }
    args[i] = 0;

    if (flag_p) {
      printf("run:");
      for (int t = 0; args[t]; t++)
        printf(" %s", args[t]);
      printf(" ?... ");
      char resp[4];
      if (read(0, resp, sizeof(resp)) <= 0)
        exit(0);
      if (!(resp[0] == 'y' || resp[0] == 'Y'))
        continue;
    }

    if (fork() == 0) {
      exec(args[0], args);
      fprintf(2, "xargs: exec failed\n");
      exit(1);
    }
    active++;

    // throttle to maxprocs
    if (active >= maxprocs) {
      wait(0);
      active--;
    }
  }

  // wait for remaining children
  while (active > 0) {
    wait(0);
    active--;
  }

  exit(0);
}
