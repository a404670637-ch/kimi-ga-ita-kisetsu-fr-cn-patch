/* hookstub3.S - safe tail-jump hook for UnivUI CS5RFont::GetCachedFont
 * entry: ecx=this, [esp+4]=uChar, [esp+8]=node ; logs (this,uChar) then runs original.
 */
    .text
    .globl _font_entry
_font_entry:
    pushl %eax
    pushl %ecx
    pushl %edx
    movl 16(%esp), %eax
    movl 4(%esp), %ecx
    pushl %ecx
    pushl %eax
    call _font_probe2
    addl $8, %esp
    popl %edx
    popl %ecx
    popl %eax
    jmp *_font_tramp

    .data
    .globl _font_tramp
_font_tramp:
    .long 0

