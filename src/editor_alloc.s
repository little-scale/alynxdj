; Cold pool allocation helpers for physical-B mint/slim-clone.
;
; A slot is available only when its record is blank AND no parent references
; it.  The reference check matters for newly minted objects: a blank chain or
; phrase is still allocated once a SONG/CHAIN cell points at it.

        .setcpu "65C02"

        .export  _find_new_chain, _find_new_phrase
        .import  _sd
        .importzp ptr1, ptr2

SONG_BYTES   = $0200
CHAINS_OFF   = $0200
PHRASES_OFF  = $0600
CHAIN_SIZE   = 32
PHRASE_SIZE  = 64
NCHAINS      = 32
NPHRASES     = 64
EMPTY        = $FF

        .segment "CODE"

; unsigned char find_new_chain(void)
; Lowest chain with 16 empty steps and no reference in song[128][4].
_find_new_chain:
        lda     #<(_sd + CHAINS_OFF)
        sta     ptr1
        lda     #>(_sd + CHAINS_OFF)
        sta     ptr1+1
        ldx     #0                      ; candidate chain
@candidate:
        ldy     #0
@blank:
        lda     (ptr1),y                ; phrase byte (skip TSP)
        cmp     #EMPTY
        bne     @next
        iny
        iny
        cpy     #CHAIN_SIZE
        bcc     @blank

        lda     #<(_sd)
        sta     ptr2
        lda     #>(_sd)
        sta     ptr2+1
        ldy     #0
@referenced:
        txa
        cmp     (ptr2),y
        beq     @next
        iny
        bne     @referenced
        inc     ptr2+1
        lda     ptr2+1
        cmp     #>(_sd + SONG_BYTES)
        bne     @referenced
        txa                             ; blank and unreferenced
        ldx     #0
        rts

@next: clc
        lda     ptr1
        adc     #CHAIN_SIZE
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       inx
        cpx     #NCHAINS
        bcc     @candidate
        lda     #EMPTY
        ldx     #0
        rts

; unsigned char find_new_phrase(void)
; Lowest phrase with no notes/commands and no reference in any chain step.
_find_new_phrase:
        lda     #<(_sd + PHRASES_OFF)
        sta     ptr1
        lda     #>(_sd + PHRASES_OFF)
        sta     ptr1+1
        ldx     #0                      ; candidate phrase
@candidate:
        ldy     #0
@blank:
        lda     (ptr1),y                ; note
        bne     @next
        iny
        iny
        lda     (ptr1),y                ; command
        bne     @next
        iny
        iny
        cpy     #PHRASE_SIZE
        bcc     @blank

        lda     #<(_sd + CHAINS_OFF)
        sta     ptr2
        lda     #>(_sd + CHAINS_OFF)
        sta     ptr2+1
        ldy     #0
@referenced:
        txa
        cmp     (ptr2),y                ; phrase byte (skip TSP)
        beq     @next
        iny
        iny
        bne     @referenced
        inc     ptr2+1
        lda     ptr2+1
        cmp     #>(_sd + PHRASES_OFF)
        bne     @referenced
        txa                             ; blank and unreferenced
        ldx     #0
        rts

@next: clc
        lda     ptr1
        adc     #PHRASE_SIZE
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       inx
        cpx     #NPHRASES
        bcc     @candidate
        lda     #EMPTY
        ldx     #0
        rts
