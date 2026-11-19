    .text
    .globl _hook_entry
_hook_entry:
    pushal
    pushfl
    movl %esp, %eax
    addl $0x24, %eax
    pushl %eax
    call _capture_text
    addl $4, %esp
    popfl
    popal
    call *0x1a8(%edi)
    pushal
    pushfl
    call _restore_text2
    popfl
    popal
    jmp *_hook_ret
