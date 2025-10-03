# exit_shellcode.s
.section .text
.globl _start

_start:
    addi  a7,x0,2        
    ecall