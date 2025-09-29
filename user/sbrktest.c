#include "kernel/types.h"
#include "user/user.h"

int main(){
    char* ptr = sbrk(1);
    printf("value at ptr : %c, ptr+10: %c\n",*ptr,*(ptr+10));
    *ptr = 'a';
    *(ptr+10) = 'b';
    printf("value at ptr : %c, ptr+10: %c\n",*ptr,*(ptr+10));
}