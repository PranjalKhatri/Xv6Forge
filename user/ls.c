#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  buf[sizeof(buf)-1] = '\0';
  return buf;
}

// Convert file type to string
char*
type_to_string(int type)
{
  switch(type){
    case T_DIR:     return "dir ";
    case T_FILE:    return "file";
    case T_DEVICE:  return "dev ";
    case T_SYMLINK: return "link";
    default:        return "?   ";
  }
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // Open normally - follow symlinks for the main path
  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
  case T_SYMLINK:
    printf("%s\t%s\t%d\t%d\n", fmtname(path), type_to_string(st.type), st.ino, (int) st.size);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      
      // Open with O_NOFOLLOW to get symlink info without following
      int tmpfd = open(buf, O_RDONLY | O_NOFOLLOW);
      if(tmpfd < 0){
        printf("ls: cannot open %s\n", buf);
        continue;
      }
      if(fstat(tmpfd, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        close(tmpfd);
        continue;
      }
      close(tmpfd);
      printf("%s\t%s\t%d\t%d\n", fmtname(buf), type_to_string(st.type), st.ino, (int) st.size);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit(0);
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit(0);
}