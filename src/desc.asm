_TEXT SEGMENT

PUBLIC get_idt
PUBLIC get_gdt
PUBLIC get_ldt
PUBLIC get_tr

; sidt m16&64 : 0F 01 /1, modrm 00 001 001 -> [rcx]
get_idt PROC
    db 00Fh, 001h, 009h
    ret
get_idt ENDP

; sgdt m16&64 : 0F 01 /0, modrm 00 000 001 -> [rcx]
get_gdt PROC
    db 00Fh, 001h, 001h
    ret
get_gdt ENDP

; sldt ax : 0F 00 /0, modrm 11 000 000
get_ldt PROC
    db 00Fh, 000h, 0C0h
    ret
get_ldt ENDP

; str ax : 0F 00 /1, modrm 11 001 000
get_tr PROC
    db 00Fh, 000h, 0C8h
    ret
get_tr ENDP

_TEXT ENDS
END
