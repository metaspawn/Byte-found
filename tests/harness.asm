; Bytefound test harness - boots, calls main() and prints AX
bits 16
org 0x7c00

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    call main
    call print_ax

    cli
.hang:
    hlt
    jmp .hang

; print the value of AX in decimal
print_ax:
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
    test ax, ax
    jnz .loop
    mov al, '0'
    call putc
    jmp .done
.loop:
    xor dx, dx
    div bx                  ; ax = ax/10, dx = remainder
    add dl, '0'
    push dx
    inc cx
    test ax, ax
    jnz .loop
.pop:
    pop ax
    call putc
    loop .pop
.done:
    pop dx
    pop cx
    pop bx
    ret

putc:
    push ax
    push bx
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    pop bx
    pop ax
    ret

%include "test.asm"

times 510-($-$$) db 0
dw 0xaa55
