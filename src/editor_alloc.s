; Cold pool allocation helpers for physical-B mint/slim-clone.
;
; A slot is available only when its record is blank AND no parent references
; it.  The reference check matters for newly minted objects: a blank chain or
; phrase is still allocated once a SONG/CHAIN cell points at it.

        .setcpu "65C02"

        .export  _find_new_chain, _find_new_phrase
        .export  _instr_taps, _reset_instr_taps, _clock_tap_glide
        .import  _sd, _voices, _live_taps, aslax4
        .importzp ptr1, ptr2

SONG_BYTES   = $0200
CHAINS_OFF   = $0200
PHRASES_OFF  = $0600
CHAIN_SIZE   = 32
PHRASE_SIZE  = 64
NCHAINS      = 32
NPHRASES     = 64
EMPTY        = $FF
INSTRS_OFF   = $1600
INSTR_TAPSLO = 5
INSTR_TAPSHI = 9
VOICE_TAPS   = 20
VOICE_TAPRATE = 22
VOICE_INUM   = 32
VOICE_SIZE   = 49

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

        .segment "HICODE1"

; unsigned __fastcall__ instr_taps(unsigned char inum)
; Records are 16 bytes, so aslax4 gives the byte offset and its page carry.
_instr_taps:
        ldx     #0
        jsr     aslax4
        sta     ptr1
        txa
        ora     #>(_sd + INSTRS_OFF)    ; page is $EA/$EB for instruments 0-31
        sta     ptr1+1
        ldy     #INSTR_TAPSHI
        lda     (ptr1),y
        and     #1
        pha
        ldy     #INSTR_TAPSLO
        lda     (ptr1),y
        plx
        rts

; void __fastcall__ reset_instr_taps(struct voice *v)
; Restore the active patch's raw 9-bit value, then update FEEDBACK/control
; live.  live_taps deliberately does not rewrite the running shift register.
_reset_instr_taps:
        sta     ptr2
        stx     ptr2+1
        ldy     #VOICE_INUM
        lda     (ptr2),y
        jsr     _instr_taps
        ldy     #VOICE_TAPS
        sta     (ptr2),y
        iny
        txa
        sta     (ptr2),y
        lda     ptr2
        ldx     ptr2+1
        jmp     _live_taps

        .segment "MIDICODE"

; void __fastcall__ clock_tap_glide(struct voice *v)
; Count this track's signed G period.  The caller selects tick or row clocks
; from the raw magnitude.  $C0FC-$C0FF holds a normalized signed countdown:
; magnitudes 1..7 reload unchanged, while magnitude 8+ reloads as magnitude-7
; so 8 means one row.  Deriving the track index here is smaller than cc65's
; repeated fixed-address indexed-pointer sequence.
_clock_tap_glide:
        sta     ptr2
        stx     ptr2+1
        sec
        sbc     #<_voices
        tay
        lda     ptr2+1
        sbc     #>_voices
        bne     @done                   ; defensive: pointer is not a voice
        tya
        ldx     #0
@index:
        cmp     #VOICE_SIZE
        bcc     @rate
        sbc     #VOICE_SIZE
        inx
        bra     @index

@rate: ldy     #VOICE_TAPRATE
        lda     (ptr2),y
        bmi     @negative
        dec     $C0FC,x
        bne     @done
        bra     @step
@negative:
        inc     $C0FC,x                 ; two's-complement count rises to 0
        bne     @done
@step:  tay                             ; preserve raw sign/direction
        bmi     @reload_negative
        cmp     #8                      ; +1..+7: raw tick period
        bcc     @reload
        sbc     #7                      ; +8..+127: 1..120 rows
        bra     @reload
@reload_negative:
        cmp     #$F9                    ; -1..-7: raw tick period
        bcs     @reload
        adc     #7                      ; -8..-128: -1..-121 rows
@reload:
        sta     $C0FC,x
        tya                             ; restore N from raw signed parameter
        bmi     @down

        ldy     #VOICE_TAPS             ; wrap +1 across the 9-bit tap value
        lda     (ptr2),y
        inc     a
        sta     (ptr2),y
        bne     @publish
        iny
        lda     (ptr2),y
        eor     #1
        sta     (ptr2),y
        bra     @publish

@down:  ldy     #VOICE_TAPS             ; wrap -1 across the 9-bit tap value
        lda     (ptr2),y
        bne     :+
        iny
        lda     (ptr2),y
        eor     #1
        sta     (ptr2),y
        dey
        lda     (ptr2),y
:       dec     a
        sta     (ptr2),y

@publish:
        lda     ptr2
        ldx     ptr2+1
        jmp     _live_taps              ; do not reseed the running LFSR
@done:  rts

        .segment "CODE"

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
