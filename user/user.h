#define SBRK_ERROR ((char *)-1)
#define KEY_UP    1001
#define KEY_DOWN  1002
#define KEY_LEFT  1003
#define KEY_RIGHT 1004
#define KEY_HOME  1005 

#define STDIN 0
#define STDOUT 1
#define STERR 2

struct stat;
struct pagestat;
struct procinfo;
struct cons_state;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sys_sbrk(int,int);
int pause(int);
int uptime(void);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
char* gets(char*, int max);
int getdelim(int, char*, char, int max);
uint strlen(const char*);
char* strcat(char *dest, const char *src);
void* memset(void*, int, uint);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char* sbrk(int);
char* sbrklazy(int);

// printf.c
void fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3)));
void printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));

// umalloc.c
void* malloc(uint);
void free(void*);

int getfreemem(void);
int getpagestat(int pid, struct pagestat *st);
int dumpmru(int);
int setreplacement_policy(int);
int getprocinfo(struct procinfo*);
int symlink(const char*,const char*);
int setconsmode(int);
int ConsSetFlag(int,int);
int GetConsState(struct cons_state*);
int SetConsState(struct cons_state*);
int getch();