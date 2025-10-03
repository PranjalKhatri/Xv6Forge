#include "kernel/types.h"
#include "user/user.h"
int main() { 
    int n;
    printf("address of n %p\n",&n);
    return 0; 
}
/*
more complex version
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Global variables - in standard xv6, these will use absolute addressing
// but the pattern is what GOT would handle in PIC
int global_counter = 0;
char global_message[] = "Testing global access";
int global_array[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
extern int wait(int*);
extern int exit(int);
extern int fork();
extern void printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));

// Global function pointer
int (*func_ptr)(int, int);

// Functions to be called via pointer
int
add(int a, int b)
{
    return a + b;
}

int
multiply(int a, int b)
{
    return a * b;
}

// Access global variables from different functions
void
modify_globals(void)
{
    int i;
    global_counter++;
    
    for(i = 0; i < 10; i++) {
        global_array[i] += 1;
    }
}

void
read_globals(void)
{
    int i;
    printf( "Counter: %d\n", global_counter);
    printf( "Message: %s\n", global_message);
    printf( "Array values: ");
    for(i = 0; i < 10; i++) {
        printf( "%d ", global_array[i]);
    }
    printf( "\n");
}

// Test function pointers (simulates PLT/GOT usage)
void
test_function_pointers(void)
{
    int result;
    
    printf( "\n=== Testing Function Pointers ===\n");
    
    func_ptr = add;
    result = (*func_ptr)(10, 5);
    printf( "add(10, 5) = %d\n", result);
    
    func_ptr = multiply;
    result = (*func_ptr)(10, 5);
    printf( "multiply(10, 5) = %d\n", result);
}

// Test globals across fork
void
test_fork_globals(void)
{
    int pid;
    
    printf( "\n=== Testing Globals Across Fork ===\n");
    
    global_counter = 42;
    printf( "Parent before fork: counter = %d\n", global_counter);
    
    pid = fork();
    if(pid < 0) {
        printf( "fork failed\n");
        exit(1);
    }
    
    if(pid == 0) {
        // Child
        printf( "Child sees: counter = %d\n", global_counter);
        global_counter = 100;
        printf( "Child modified: counter = %d\n", global_counter);
        exit(0);
    } else {
        // Parent
        wait(0);
        printf( "Parent after child: counter = %d (should still be 42)\n", global_counter);
    }
}

int
main(int argc, char *argv[])
{
    int i;
    
    printf( "=== xv6 Global Data Access Test ===\n");
    printf( "This demonstrates patterns handled by GOT in PIC\n\n");
    
    // Initial state
    printf( "=== Initial State ===\n");
    read_globals();
    
    // Modify globals
    printf( "\n=== After Modifications ===\n");
    for(i = 0; i < 3; i++) {
        modify_globals();
    }
    read_globals();
    
    // Test function pointers
    test_function_pointers();
    
    // Test fork behavior
    test_fork_globals();
    
    printf( "\n=== Test Complete ===\n");
    exit(0);
}
*/
