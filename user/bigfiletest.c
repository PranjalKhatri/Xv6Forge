#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define BLOCKSIZE 1024

int
main(int argc, char *argv[])
{
    int fd;
    char buf[BLOCKSIZE];
    int i, j;
    int filesize_kb;

    if(argc < 2){
        printf("Usage: bigfile <size_in_KB>\n");
        exit(1);
    }

    filesize_kb = atoi(argv[1]);
    int FILESIZE = filesize_kb * 1024;

    printf("Creating bigfile: size: %d KB...\n", filesize_kb);

    fd = open("bigfile", O_CREATE | O_RDWR);
    if(fd < 0){
        printf("Failed to create file\n");
        exit(2);
    }

    // Fill buffer with a pattern
    for(i = 0; i < BLOCKSIZE; i++)
        buf[i] = (char)(i % 256);

    // Write sequentially until FILESIZE
    for(i = 0; i < FILESIZE / BLOCKSIZE; i++){
        if(write(fd, buf, BLOCKSIZE) != BLOCKSIZE){
            printf("Write failed at block %d\n", i);
            exit(3);
        }
    }

    printf("Write complete. Verifying...\n");

    // Reset file offset
    close(fd);
    fd = open("bigfile", O_RDONLY);
    if(fd < 0){
        printf("Failed to open file for read\n");
        exit(4);
    }

    // Read back and verify
    for(i = 0; i < FILESIZE / BLOCKSIZE; i++){
        char rbuf[BLOCKSIZE];
        if(read(fd, rbuf, BLOCKSIZE) != BLOCKSIZE){
            printf("Read failed at block %d\n", i);
            exit(5);
        }
        for(j = 0; j < BLOCKSIZE; j++){
            if(rbuf[j] != (char)(j % 256)){
                printf("Data mismatch at block %d, byte %d\n", i, j);
                exit(6);
            }
        }
    }

    printf("Verification successful!\n");
    close(fd);
    exit(0);
}
