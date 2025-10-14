#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAXLINE 256
#define MAXARGS 3

char line[MAXLINE];
char *ex_args[MAXARGS] = {
  [0] "/sh",
  [2] 0
};

int verbose = 0;
int show_args = 0;
int quiet = 0;
char *out_file = 0;

// Print usage info
void usage(void) {
  printf("Usage: psh <file> [flags]\n");
  printf("Runs each line of <file> as a shell command.\n\n");
  printf("Flags:\n");
  printf("  -v, --verbose       Show each command before executing\n");
  printf("  -a, --args          Show full command arguments passed to /sh\n");
  printf("  -o <file>           Redirect output of all commands to <file>\n");
  printf("  -q, --quiet         Suppress all output and errors\n");
  printf("  -h, --help          Show this help message\n\n");
  printf("Examples:\n");
  printf("  psh script.sh -v\n");
  printf("  psh script.sh -o out.txt -q\n");
  exit(0);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
  }

  // Parse flags
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
      verbose = 1;
    else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--args"))
      show_args = 1;
    else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
      quiet = 1;
    else if (!strcmp(argv[i], "-o")) {
      if (i + 1 >= argc) {
        fprintf(2, "psh: -o requires a file argument\n");
        exit(1);
      }
      out_file = argv[++i];
    }
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
      usage();
    else {
      fprintf(2, "psh: unknown flag %s\n", argv[i]);
      usage();
    }
  }

  // Open file
  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    if (!quiet)
      fprintf(2, "psh: unable to open %s for reading\n", argv[1]);
    exit(2);
  }

  // Redirect output if requested
  int saved_stdout = dup(1);
  int saved_stderr = dup(2);
  if (out_file) {
    int outfd = open(out_file, O_WRONLY | O_CREATE | O_TRUNC);
    if (outfd >= 0) {
      close(1);
      dup(outfd);
      close(2);
      dup(outfd);
      close(outfd);
    } else if (!quiet) {
      fprintf(2, "psh: cannot open %s for writing\n", out_file);
    }
  }

  if (!quiet)
    printf("psh executing: %s\n", argv[1]);

  // Skip shebang line
  getdelim(fd, line, '\n', MAXLINE);

  // Executes each command line
  while (getdelim(fd, line, '\n', MAXLINE)) {
    if (verbose && !quiet)
      printf(">> %s", line);

    int pid = fork();
    if (pid == 0) {
      ex_args[1] = line;

      if (show_args && !quiet) {
        printf("[ARGS] /sh \"%s\"\n", line);
      }

      exec("/sh", ex_args);
      if (!quiet)
        fprintf(2, "psh: exec /sh failed for: %s\n", line);
      exit(1);
    }
    if (pid > 0)
      wait(0);
    if (pid < 0) {
      if (!quiet)
        fprintf(2, "psh: fork failed\n");
      exit(1);
    }
  }

  // Restore stdout/stderr
  if (out_file) {
    close(1);
    dup(saved_stdout);
    close(2);
    dup(saved_stderr);
  }

  close(saved_stdout);
  close(saved_stderr);
  close(fd);

  return 0;
}
