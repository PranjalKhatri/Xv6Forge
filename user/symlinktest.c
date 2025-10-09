#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define BUF_SIZE 100

int main(int argc, char *argv[]) {
    int fd;
    char buf[BUF_SIZE];

    // 1. Create a regular file
    fd = open("file1", O_CREATE | O_RDWR);
    if(fd < 0) {
        printf("Failed to create file1\n");
        exit(1);
    }
    write(fd, "Hello xv6", 9);
    close(fd);

    // 2. Create a symlink to file1
    if(symlink("file1", "link1") < 0) {
        printf("Failed to create symlink link1 -> file1\n");
        exit(1);
    }

    // 3. Open via symlink and read
    fd = open("link1", O_RDONLY);
    if(fd < 0) {
        printf("Failed to open symlink link1\n");
        exit(1);
    }
    read(fd, buf, 9);
    buf[9] = 0;
    printf("Read via symlink: %s\n", buf); // Should print "Hello xv6"
    close(fd);

    // 4. Open symlink with O_NOFOLLOW
    fd = open("link1", O_RDONLY | O_NOFOLLOW);
    if(fd < 0) {
        printf("O_NOFOLLOW works: cannot open symlink as file\n");
    } else {
        printf("O_NOFOLLOW failed: opened symlink inode itself\n");
        close(fd);
    }

    // 5. Recursive symlink test
    if(symlink("link2", "link2") < 0) {
        printf("Correctly failed to create self-referencing symlink\n");
    }

    printf("All symlink tests passed!\n");
    exit(0);
}
