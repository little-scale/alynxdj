; Cold pool allocation helpers for physical-B mint/slim-clone.
;
; A slot is available only when its record is blank AND no parent references
; it.  The reference check matters for newly minted objects: a blank chain or
; phrase is still allocated once a SONG/CHAIN cell points at it.

        .setcpu "65C02"

        .export  _find_new_chain, _find_new_phrase, _engine_table_cursor
        .export  _ifield_screen_y, _instr_selector
        .export  _scale_pcm, _pool_trigger, _pool_cancel
        .export  _instr_taps, _reset_instr_taps, _clock_tap_glide
        .export  _sel_paint
        .import  _sd, _voices, _live_taps, _ifield_type, aslax4
        .import  _edit_instr, _i_row
        .import  _screen, _s_row, _c_row, _p_row, _sel_anchor
        .import  _draw_song_screen, _draw_chain_screen, _draw_phrase_screen
        .import  _stream_cancel, _trig_kit, _trig_member
        .import  _dac_mode, _dac_off, _pcm_done, _dac_rate
        .import  popa, popax, incsp2
        .importzp ptr1, ptr2, sp, tmp1, tmp2

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
VOICE_ENV      = 2
VOICE_ENVLEVEL = 3
VOICE_TABLE    = 29
VOICE_TPOS     = 30
VOICE_INUM   = 32
VOICE_SIZE   = 49

        .segment "CODE"

; Point ptr1 at the current 16-byte instrument.
instr_ptr:
        lda     _edit_instr
        ldx     #0
        jsr     aslax4
        clc
        adc     #<(_sd + INSTRS_OFF)
        sta     ptr1
        txa
        adc     #>(_sd + INSTRS_OFF)
        sta     ptr1+1
        rts

; unsigned char __fastcall__ instr_selector(unsigned char dir)
; Shared TYPE/BANK editor. $FF is a read request used by drawing; it also
; repairs legacy KIT instruments whose shared WAV/KIT byte was left at $FF.
; KIT is total over 00-07: entering KIT selects 00 if needed and decrementing
; 00 remains 00. WAV retains its useful $FF integrate-triangle selection.
_instr_selector:
        pha
        jsr     instr_ptr
        pla
        sta     tmp1
        lda     tmp1
        cmp     #EMPTY
        beq     @normalise
        lda     _i_row
        beq     @type
        cmp     #1
        beq     @volume
        jmp     @bank

@type:
        ldy     #0                      ; TYPE: previous/next shipped type
        lda     (ptr1),y
        and     #3
        ldx     tmp1
        beq     @type_next
        cpx     #3
        bne     @type_lookup
@type_next:
        clc
        adc     #4
@type_lookup:
        tax
        lda     @type_step,x
        sta     (ptr1),y
        cmp     #3
        bne     @return_type

@normalise:
        ldy     #0
        lda     (ptr1),y
        and     #3
        cmp     #3
        bne     @return_bank
        ldy     #4
        lda     (ptr1),y
        cmp     #8
        bcc     @done
        lda     #0
        sta     (ptr1),y
@done:
        rts

@return_type:
        rts

@return_bank:
        ldy     #4
        lda     (ptr1),y
        rts

@volume:
        ldy     #0
        lda     (ptr1),y
        and     #3
        cmp     #3
        bne     @volume_full

        ldx     tmp1                    ; KIT: only Up/Down edit high nibble
        cpx     #2
        bcs     @done
        ldy     #1
        lda     (ptr1),y
        and     #$F0
        cpx     #0
        bne     @volume_down
        cmp     #$70
        bcs     @volume_store
        clc
        adc     #$10
        bra     @volume_store
@volume_down:
        cmp     #$10
        bcc     @volume_zero
        sec
        sbc     #$10
        bra     @volume_store

@volume_full:
        ldy     #1                      ; LFSR/WAV retain 00-7F fine editing
        lda     (ptr1),y
        ldx     tmp1
        beq     @volume_coarse_up
        dex
        beq     @volume_coarse_down
        dex
        beq     @volume_fine_down
        cmp     #$7F
        bcs     @done
        inc     a
        bra     @volume_store
@volume_fine_down:
        beq     @done
        dec     a
        bra     @volume_store
@volume_coarse_up:
        cmp     #$70
        bcc     @volume_add_16
        lda     #$7F
        bra     @volume_store
@volume_add_16:
        clc
        adc     #$10
        bra     @volume_store
@volume_coarse_down:
        cmp     #$10
        bcc     @volume_zero
        sec
        sbc     #$10
        bra     @volume_store
@volume_zero:
        lda     #0
@volume_store:
        sta     (ptr1),y
        rts

@bank:
        ldy     #0
        lda     (ptr1),y
        and     #3
        cmp     #2
        bcc     @return_bank            ; LFSR has no selector
        sta     tmp2                    ; WAV=2, KIT=3
        ldy     #4
        lda     (ptr1),y
        ldx     tmp1
        beq     @bank_up
        cpx     #3
        beq     @bank_up

        cmp     #1
        bcc     @bank_floor
        cmp     #8
        bcs     @bank_floor
        dec     a
        bra     @bank_store
@bank_floor:
        ldx     tmp2
        cpx     #3
        beq     @bank_zero
        lda     #EMPTY
        bra     @bank_store

@bank_up:
        cmp     #8
        bcs     @bank_zero
        cmp     #7
        bcc     :+
        rts
:
        inc     a
        bra     @bank_store
@bank_zero:
        lda     #0
@bank_store:
        sta     (ptr1),y
        rts

@type_step:
        .byte   3, 3, 0, 2              ; previous
        .byte   2, 2, 3, 0              ; next

; void sel_paint(void)
; Publish the visible selection rows for draw_char(), then repaint the
; hierarchy grid. Every glyph cell on those rows receives the same inverse
; accent treatment without duplicating the three C row renderers.
_sel_paint:
        lda     _screen
        beq     @song
        cmp     #1
        beq     @chain
        lda     _p_row
        bra     @local
@chain:
        lda     _c_row
@local:
        stz     ptr2                    ; CHAIN/PHRASE page starts at row 0
        bra     @bounds
@song:
        lda     _s_row
        and     #$F0
        sta     ptr2                    ; current SONG page
        lda     _s_row

@bounds:
        cmp     _sel_anchor
        bcc     @cursor_first
        sta     tmp2                    ; cursor is the high endpoint
        lda     _sel_anchor
        sta     tmp1
        bra     @clip
@cursor_first:
        sta     tmp1
        lda     _sel_anchor
        sta     tmp2

@clip:
        lda     tmp1
        sec
        sbc     ptr2
        bcs     :+
        lda     #0
:       inc     a
        sta     $C8FE                   ; first selected character row
        lda     tmp2
        sec
        sbc     ptr2
        cmp     #16
        bcc     :+
        lda     #15
:       inc     a
        sta     $C8FF                   ; last selected character row

        lda     _screen
        beq     @draw_song
        cmp     #1
        beq     @draw_chain
        jmp     _draw_phrase_screen
@draw_chain:
        jmp     _draw_chain_screen
@draw_song:
        jmp     _draw_song_screen

; unsigned char __fastcall__ engine_table_cursor(unsigned table_track)
; AX arrives as table:track.  tpos normally points one step beyond the row
; most recently applied, so fold it back for the display.  Low row zero is
; the full-table F->0 wrap for every clock mode.  Return $FF if this
; track/table is not sounding.
_engine_table_cursor:
        stx     ptr1+1                  ; requested table
        ldx     $C011                   ; eng_mode
        beq     @no_table
        cmp     #4
        bcs     @no_table
        tay                             ; track -> voice byte offset
        lda     #0
        cpy     #0
        beq     @voice
@offset:
        clc
        adc     #VOICE_SIZE
        dey
        bne     @offset
@voice:
        tay
        lda     _voices+VOICE_ENV,y
        beq     @no_table
        lda     _voices+VOICE_TABLE,y
        cmp     ptr1+1
        bne     @no_table
        lda     _voices+VOICE_TPOS,y
        and     #$0F
        bne     @nonzero
        lda     #$0F                    ; every mode just applied row F
        rts
@nonzero:
        dec     a
@done:
        rts
@no_table:
        lda     #EMPTY
        rts

; unsigned char __fastcall__ ifield_screen_y(unsigned char field)
; Each packed byte is type mask:LFSR/legacy-01/WAV/KIT in bits 7..4 and
; fixed screen row in bits 3..0. Return zero when the type omits the field.
_ifield_screen_y:
        tay
        ldx     _ifield_type
        lda     @type_bit,x
        and     @field_layout,y
        beq     @field_hidden
        lda     @field_layout,y
        and     #$0F
        rts
@field_hidden:
        lda     #0
        rts

        .segment "RODATA"
@field_layout:
        .byte   $F2, $F3, $74, $75, $76, $F7, $38, $39
        .byte   $3A, $3B, $3C, $CD, $7E, $7F, $F1

        .segment "HICODE1"
@type_bit:
        .byte   $10, $20, $40, $80

        .segment "HICODE3"

; void __fastcall__ scale_pcm(unsigned char *dst, unsigned char n,
;                             unsigned char shift)
; Called only for shifts 1, 2, or 4. Preserve the sign bit; shift 4
; is the explicit VOL 00 mute case.  Scaling happens outside the DAC IRQ.
; Walk backward so Y can hold the byte count, and test mute once per piece
; instead of burdening every sample in the ordinary shift loop.
_scale_pcm:
        sta     ptr2+1                 ; shift / mute code
        jsr     popa
        pha                             ; popax uses Y internally
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        pla
        tay                             ; byte count (always 1-64)
        lda     ptr2+1
        cmp     #4
        beq     @mute_byte
@scale_byte:
        dey
        lda     (ptr1),y
        ldx     ptr2+1
@scale_shift:
        cmp     #$80                   ; carry = original sign bit
        ror     a
        dex
        bne     @scale_shift
        sta     (ptr1),y
        tya
        bne     @scale_byte
        rts
@mute_byte:
        lda     #0
@mute_loop:
        dey
        sta     (ptr1),y
        bne     @mute_loop              ; STA preserves DEY's zero flag
        rts

        .segment "HICODE1"

; void __fastcall__ pool_trigger(unsigned char voice, unsigned char kit,
;                                unsigned char member)
; IRQ-context latch. Resolve the slot's owner/instrument VOL, convert it to
; 0/1/2 arithmetic shifts (or 4=mute), pack it above kit, publish last.
_pool_trigger:
        pha                             ; member (slots are internal 0/1 only)
        ldy     #1
        lda     (sp),y
        tax                             ; voice
        phx                             ; retain DAC slot
        lda     _dac_off,x
        lsr     a
        lsr     a
        lsr     a                       ; physical channel 0-3
        tax
        ldy     @voice_offset,x
        lda     _voices+VOICE_ENVLEVEL,y
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        beq     @kit_mute               ; low nibble never affects KIT gain
        lsr     a
        eor     #3
        inc     a
        lsr     a                       ; 60-7F=0, 20-5F=1, 01-1F=2
        bra     @kit_shift
@kit_mute:
        lda     #4
@kit_shift:
        asl     a
        asl     a
        asl     a
        sta     tmp1
        plx                             ; DAC slot
        stz     _stream_cancel,x
        stz     _pcm_done,x
        lda     #191                    ; canonical 5,208.333 Hz KIT rate
        sta     _dac_rate,x             ; same-row S may override it
        lda     #1
        sta     _dac_mode,x             ; reserve until cart pump starts
        inc     $C02B,x
        pla                             ; member
        sta     _trig_member,x
        ldy     #0
        lda     (sp),y
        ora     tmp1
        sta     _trig_kit,x             ; publish last
        jmp     incsp2

@voice_offset:
        .byte   0, VOICE_SIZE, VOICE_SIZE*2, VOICE_SIZE*3

        .segment "CODE"

; void __fastcall__ pool_cancel(unsigned char voice)
_pool_cancel:
        cmp     #2
        bcs     @cancel_done
        tax
        lda     #$FF
        sta     _trig_kit,x
        lda     #1
        sta     _stream_cancel,x
@cancel_done:
        rts

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
