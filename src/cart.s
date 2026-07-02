; Cart streaming reader (the sample pool lives past the loaded program).
;
; Modeled on cc65's lynx-cart.s (Bastian Schick / Karri Kaksonen): a block
; select shifts 8 address bits out via IODAT bit 1, clocked by SYSCTL1
; strobes; RCART0 then reads bytes sequentially within the 1024-byte
; block. cart_read auto-advances to the next block, so callers see a flat
; byte stream. Not IRQ-safe (the PCM ring pump calls it from the main
; loop only); EEPROM ops share the bus but only run while stopped.

        .export         _cart_seek
        .export         _cart_read
        .import         popa, popax
        .importzp       ptr1

        .include        "lynx.inc"

BLOCKSIZE = 1024

        .bss
cur_block:  .res 1
left_lo:    .res 1              ; bytes left in the current block
left_hi:    .res 1
tmp_n:      .res 1

        .code

; select block A and reset the in-block counter
select:
        sta     cur_block
        pha
        phx
        phy
        lda     #$0b & $fc      ; IODAT rest state without CART data bits
        tay
        ora     #2
        tax
        lda     cur_block
        sec
        bra     @2
@0:     bcc     @1
        stx     IODAT
        clc
@1:     inx
        stx     SYSCTL1
        dex
@2:     stx     SYSCTL1
        rol     a
        sty     IODAT
        bne     @0
        lda     #$0b
        sta     IODAT
        lda     #<BLOCKSIZE
        sta     left_lo
        lda     #>BLOCKSIZE
        sta     left_hi
        ply
        plx
        pla
        rts

; void __fastcall__ cart_seek(unsigned char block, unsigned off);
; off < 1024: skip into the block
_cart_seek:
        sta     ptr1            ; off lo
        stx     ptr1+1
        jsr     popa            ; block
        jsr     select
@skip:  lda     ptr1
        ora     ptr1+1
        beq     @done
        lda     RCART0          ; discard
        dec     left_lo
        bne     :+
        lda     left_hi
        beq     @next
        dec     left_hi
:       lda     ptr1
        bne     :+
        dec     ptr1+1
:       dec     ptr1
        bra     @skip
@next:  lda     cur_block       ; block exhausted mid-skip (off < 1024 so
        inc     a               ; this only fires at exactly 1024)
        jsr     select
        bra     @skip
@done:  rts

; void __fastcall__ cart_read(unsigned char *dst, unsigned char n);
; n = 1..255 bytes to *dst, auto-advancing blocks
_cart_read:
        sta     tmp_n           ; n (popax clobbers A/X/Y)
        jsr     popax           ; dst
        sta     ptr1
        stx     ptr1+1
        ldx     tmp_n
        beq     @out
        ldy     #0
@loop:  lda     left_lo
        ora     left_hi
        beq     @adv
        lda     RCART0
        sta     (ptr1),y
        iny
        ; dec 16-bit left
        lda     left_lo
        bne     :+
        dec     left_hi
:       dec     left_lo
        dex
        bne     @loop
@out:   rts
@adv:   lda     cur_block
        inc     a
        jsr     select
        bra     @loop
