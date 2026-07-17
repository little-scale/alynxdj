; 93C86 EEPROM driver (16 Kbit = 1024 x 16), 10-bit addressing.
;
; Modeled byte-for-byte on cc65's 93C46 driver (Bastian Schick / Shawn
; Jefferson / Ullrich von Bassewitz) with the command stretched from 9 to
; 13 bits: start + 2-bit opcode + 10-bit address. Wiring is the standard
; Lynx cart scheme: CS = cart counter A7 (reset via SYSCTL1, driven high
; by strobing RCART0 128 times), CLK = A1 (2 strobes per level), data =
; AUDIN (IODAT bit 4, direction via IODIR).
;
; The special-command don't-care bits deliberately match cc65's hardware
; driver exactly.  Some SD-cart EEPROM emulators decode the canonical full
; command even though a physical 93C86 only requires its leading bits.

        .export         _ee_read
        .export         _ee_write
        .import         popax
        .importzp       ptr1, ptr2

        .include        "lynx.inc"

; 93CX6 opcodes (bits 11..10 of the 12-bit command word)
EE_OP_READ  = %10
EE_OP_WRITE = %01
EE_OP_EWEN  = %00       ; + address top bits 11
EE_START    = $10       ; start bit = bit 12 of the command word

; ------------------------------------------------------------------------
; unsigned __fastcall__ ee_read (unsigned cell);   /* cell 0..1023 */

_ee_read:
        sta     ptr2
        txa
        and     #$03
        ora     #(EE_OP_READ << 2) | EE_START
        sta     ptr2+1
        jsr     EE_Send13Bit

        lda     #$0a
        sta     IODIR           ; AUDIN to input

        clc
        stz     ptr1
        stz     ptr1+1
        ldy     #16-1
@L1:    stz     RCART0          ; CLK = 1
        stz     RCART0
        stz     RCART0          ; CLK = 0
        stz     RCART0
        lda     IODAT
        and     #$10
        adc     #$f0            ; C = 1 iff bit set
        rol     ptr1
        rol     ptr1+1
        dey
        bpl     @L1

        ldx     #$1a
        stx     IODIR           ; AUDIN back to output
        ldx     #3              ; CS low (reset the cart counter)
        stx     SYSCTL1
        dex
        stx     SYSCTL1

        lda     ptr1
        ldx     ptr1+1
        rts

; ------------------------------------------------------------------------
; void __fastcall__ ee_write (unsigned cell, unsigned val);

_ee_write:
        sta     ptr1
        stx     ptr1+1          ; val
        lda     #$ff            ; EWEN: op 00, addr = 1111111111
        sta     ptr2
        lda     #(EE_OP_EWEN << 2) | EE_START | %11
        sta     ptr2+1
        jsr     EE_Send13Bit

        jsr     popax           ; cell
        sta     ptr2
        txa
        and     #$03
        ora     #(EE_OP_WRITE << 2) | EE_START
        sta     ptr2+1
        jsr     EE_Send13Bit
        jsr     EE_Send16Bit    ; data from ptr1 (CS stays high)

; wait for ready: CS high again, poll DO
        ldx     #63
@cs:    stz     RCART0
        stz     RCART0
        dex
        bpl     @cs
        lda     #$0a
        sta     IODIR           ; AUDIN to input
        lda     #$10
@busy:  bit     IODAT
        beq     @busy
        lda     #$1a
        sta     IODIR

        lda     #$00            ; EWDS: op 00, addr 00xxxxxxxx
        sta     ptr2
        lda     #(EE_OP_EWEN << 2) | EE_START
        sta     ptr2+1
        ; fall through into EE_Send13Bit

; ------------------------------------------------------------------------
; Send the 13-bit command in ptr2 (start bit at bit 12, then op+address).

EE_Send13Bit:
        ldx     #3              ; CS low
        stx     SYSCTL1
        dex
        stx     SYSCTL1
        ldx     #63             ; 128 strobes -> counter 128 -> CS (A7) high
@cs:    stz     RCART0
        stz     RCART0
        dex
        bpl     @cs

        asl     ptr2            ; left-align bit 12 at bit 15
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1

        ldy     #12             ; 13 bits
@bit:   asl     ptr2
        rol     ptr2+1          ; next bit -> C
        lda     #$0b
        bcc     :+
        ora     #$10
:       sta     IODAT
        stz     RCART0          ; CLK = 1
        stz     RCART0
        stz     RCART0          ; CLK = 0
        stz     RCART0
        dey
        bpl     @bit

        lda     #$0b
        sta     IODAT
        rts

; ------------------------------------------------------------------------
; Send the 16-bit value in ptr1 (MSB first); leaves CS low.

EE_Send16Bit:
        ldy     #15
@bit:   asl     ptr1
        rol     ptr1+1
        lda     #$0b
        bcc     :+
        ora     #$10
:       sta     IODAT
        stz     RCART0
        stz     RCART0
        stz     RCART0
        stz     RCART0
        dey
        bpl     @bit

        ldx     #3              ; CS low
        stx     SYSCTL1
        dex
        stx     SYSCTL1
        lda     #$0b
        sta     IODAT
        rts
