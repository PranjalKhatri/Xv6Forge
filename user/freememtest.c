#include "kernel/types.h" 
#include "user/user.h"

int main(){
    int mem = getfreemem();
    printf("free mem available is %dB, %dKB\n",mem,mem/1024);
}