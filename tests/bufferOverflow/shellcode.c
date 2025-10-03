// On your host Linux machine -> shellcode.c
// Note: unistd.h is for Linux, but we only need the compiler to generate bytes.
#include <unistd.h> 

int main() {
    // The code we want to get the bytes for.
    char *argv[] = {"sh", 0};
    execv("/bin/sh", argv); 
}
