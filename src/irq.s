; Interrupt core: VBlank plus two symmetric timer-fed DAC slots.
;
; Timer 7 feeds slot 0 and timer 5 feeds slot 1.  Either slot can be a
; cart-streamed KIT sample or a 32-byte wavetable, and dac_off[] selects
; the owning Mikey channel (A/B/C/D).  This keeps the hardware's channel
; symmetry while retaining the measured two-PCM-voice CPU ceiling (D5/D6).

        .export         _vbl_install
        .export         _frames
        .export         _pcm_stop
        .export         _dac_stop
        .export         _dac_source_rate_set
        .export         _wave_start
        .export         _wave_rate
        .export         _wave_stop
        .export         _pcm_ring_start
        .export         _pcm_ptr
        .exportzp       _pcm_head
        .export         _pcm_done
        .export         _dac_mode
        .export         _dac_off
        .export         _dac_muted
        .export         _dac_phase
        .export         _dac_step
        .import         _sd
        .import         _engine_tick
        .import         _trig_member
        .import         popa

        .include        "zeropage.inc"

INTRST   := $FD80
INTSET   := $FD81
TIM2CTLA := $FD09
TIM5BKUP := $FD14
TIM5CTLA := $FD15
TIM7BKUP := $FD1C
TIM7CTLA := $FD1D
AUD0DAC  := $FD22               ; channel A OUTPUT; channels are +8 bytes
AUD0CTL  := $FD25
SERCTL   := $FD8C
SERDAT   := $FD8D
MIDI_RX  := $C048
SYNC_RX_HEAD := $C004
SYNC_RX_TAIL := $C005
SYNC_RX_OVERRUN := $C006
SYNC_MODE := $C00F
CLOCK_RX_HEAD := $C0F9
CLOCK_RX_TAIL := $C0FA
CLOCK_RX_OVERRUN := $C0FB
CLOCK_RX := $C088
PCM_UNDERRUN := $C027             ; two saturating diagnostic counters

PCM_CTLA = $98                  ; int enable | count | reload | 1us
WAVES_OFF = 7424                ; offsetof(struct songdata, waves)
RING0_HI = $D0                  ; slot 0: $D000-$D1FF (512 bytes)
RING1_HI = $D2                  ; slot 1: $D200-$D3FF (512 bytes)

        .segment "APPZP" : zeropage
_pcm_ptr: .res 4                ; two ring tails, IRQ-owned
wav_ptr0: .res 2
wav_ptr1: .res 2
_pcm_head: .res 4               ; two ring heads, pump-owned

        .bss
_pcm_done: .res 2
_dac_mode: .res 2               ; DAC_NONE / DAC_SAMPLE / DAC_WAVE
_dac_off: .res 2                ; owning channel * 8
_dac_muted: .res 2              ; consume normally, write zero when muted
in_tick:  .res 1                ; VBL tick re-entrancy guard
zpbuf:    .res 32               ; cc65 runtime zp save (tick runs C in IRQ)
_dac_phase:
wav_pos:  .res 2               ; WAV position / KIT half-rate phase
_dac_step:
wav_step: .res 2               ; WAV step / KIT source-byte stride
tmp_slot: .res 1
tmp_w:    .res 1
tmp_clock:.res 1
tmp_bkup: .res 1
tmp_step: .res 1
_frames:  .res 2                ; u16 frame counter (read by C)

        .code

; void vbl_install(void);
_vbl_install:
        sei
        lda     #<handler
        sta     $FFFE
        lda     #>handler
        sta     $FFFF
        stz     _frames
        stz     _frames+1
        stz     PCM_UNDERRUN
        stz     PCM_UNDERRUN+1
        lda     #$9F            ; TIM2: int enable | reload | count | linked
        sta     TIM2CTLA
        cli
        rts

; void __fastcall__ dac_stop(unsigned char slot);
_dac_stop:
        cmp     #0
        bne     @one
        stz     TIM7CTLA
        phx
        ldx     _dac_off
        stz     AUD0CTL,x
        stz     AUD0DAC,x
        plx
        stz     _dac_mode
        rts
@one:   stz     TIM5CTLA
        phx
        ldx     _dac_off+1
        stz     AUD0CTL,x
        stz     AUD0DAC,x
        plx
        stz     _dac_mode+1
        rts

; void pcm_stop(void); stop both DAC slots.
_pcm_stop:
        lda     #0
        jsr     _dac_stop
        lda     #1
        jmp     _dac_stop

; void __fastcall__ pcm_ring_start(unsigned char slot);
_pcm_ring_start:
        tax
        stz     _dac_phase,x
        lda     _trig_member,x
        lsr     a
        lsr     a
        lsr     a
        sta     _dac_step,x
        txa
        bne     @one
        stz     TIM7CTLA
        phx
        ldx     _dac_off
        stz     AUD0CTL,x
        stz     AUD0DAC,x
        plx
        stz     _pcm_ptr
        lda     #RING0_HI
        sta     _pcm_ptr+1
        lda     #1
        sta     _dac_mode
        lda     #191                    ; fixed 5,208.333 Hz KIT timer
        sta     TIM7BKUP
        lda     #PCM_CTLA
        sta     TIM7CTLA
        rts
@one:   stz     TIM5CTLA
        phx
        ldx     _dac_off+1
        stz     AUD0CTL,x
        stz     AUD0DAC,x
        plx
        stz     _pcm_ptr+2
        lda     #RING1_HI
        sta     _pcm_ptr+3
        lda     #1
        sta     _dac_mode+1
        lda     #191                    ; fixed 5,208.333 Hz KIT timer
        sta     TIM5BKUP
        lda     #PCM_CTLA
        sta     TIM5CTLA
        rts

; void __fastcall__ dac_source_rate_set(unsigned char slot,
;                                        unsigned char rate);
; S uses its low two bits as 1x/2x/4x/.5x. Update both the active cursor and
; the pending trigger latch: a same-row S therefore survives cart prefill,
; while the next note/R overwrites the latch from the instrument's KIT TSP.
_dac_source_rate_set:
        pha
        jsr     popa
        tax
        lda     _dac_mode,x
        dec     a                       ; KIT(1) only; WAV(2) stays nonzero
        beq     :+
        pla
        rts
:       pla
        and     #3
        inc     a                       ; 0/1/2/3 -> step 1/2/4/0
        and     #3
        cmp     #3
        bne     :+
        inc     a
:       sta     _dac_step,x
        stz     _dac_phase,x
        asl     a                       ; pack step above pad bits
        asl     a
        asl     a
        sta     tmp_step
        lda     _trig_member,x
        and     #7
        ora     tmp_step
        sta     _trig_member,x
        rts

; void __fastcall__ wave_start(unsigned char slot, unsigned char w);
_wave_start:
        sta     tmp_w
        jsr     popa
        sta     tmp_slot
        jsr     _dac_stop
        lda     tmp_slot
        bne     @one
        stz     wav_pos
        lda     #<(_sd + WAVES_OFF)
        sta     wav_ptr0
        lda     #>(_sd + WAVES_OFF)
        sta     wav_ptr0+1
        lda     tmp_w
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        clc
        adc     wav_ptr0
        sta     wav_ptr0
        bcc     @out
        inc     wav_ptr0+1
@out:   rts
@one:   stz     wav_pos+1
        lda     #<(_sd + WAVES_OFF)
        sta     wav_ptr1
        lda     #>(_sd + WAVES_OFF)
        sta     wav_ptr1+1
        lda     tmp_w
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        clc
        adc     wav_ptr1
        sta     wav_ptr1
        bcc     @out1
        inc     wav_ptr1+1
@out1:  rts

; void wave_rate(slot, clock, bkup, step); step is the fastcall argument.
_wave_rate:
        sta     tmp_step
        jsr     popa
        sta     tmp_bkup
        jsr     popa
        sta     tmp_clock
        jsr     popa
        cmp     #0
        bne     @one
        stz     TIM7CTLA
        lda     tmp_step
        sta     wav_step
        lda     tmp_bkup
        sta     TIM7BKUP
        lda     #2
        sta     _dac_mode
        lda     tmp_clock
        ora     #$98
        sta     TIM7CTLA
        rts
@one:   stz     TIM5CTLA
        lda     tmp_step
        sta     wav_step+1
        lda     tmp_bkup
        sta     TIM5BKUP
        lda     #2
        sta     _dac_mode+1
        lda     tmp_clock
        ora     #$98
        sta     TIM5CTLA
        rts

; void __fastcall__ wave_stop(unsigned char slot);
_wave_stop:
        jmp     _dac_stop

handler:
        pha
        phx
        phy
        lda     INTSET
        and     #$10            ; timer 4 -> MIDI UART receive
        beq     @slot0check
        lda     SERCTL
        and     #$40            ; level IRQ can outlive RX ready by a cycle
        beq     @serialspurious
        lda     SERDAT          ; reading promptly prevents UART overrun
        pha                     ; level IRQ: drain RX before acknowledging it
        lda     #$10
        sta     INTRST
        pla
        ldx     SYNC_MODE
        cpx     #4              ; SYNC_IN24 uses its own live ring
        beq     @clockrx
        ldx     SYNC_RX_HEAD
        sta     MIDI_RX,x
        inx
        txa
        and     #$3F
        tax
        cpx     SYNC_RX_TAIL
        beq     @serialfull
        stx     SYNC_RX_HEAD
        bra     @serialdone
@serialfull:
        inc     SYNC_RX_OVERRUN
@serialdone:
        bra     @slot0check
@clockrx:
        ldx     CLOCK_RX_HEAD
        sta     CLOCK_RX,x
        inx
        txa
        and     #$3F
        tax
        cpx     CLOCK_RX_TAIL
        beq     @clockfull
        stx     CLOCK_RX_HEAD
        bra     @clockdone
@clockfull:
        inc     CLOCK_RX_OVERRUN
@clockdone:
        bra     @slot0check
@serialspurious:
        lda     #$10
        sta     INTRST
@slot0check:
        lda     INTSET
        and     #$80            ; timer 7 -> DAC slot 0
        beq     @slot1
@slot0:
        sta     INTRST
        lda     _dac_mode
        cmp     #1
        beq     @s0sample
        cmp     #2
        beq     @s0wave
        stz     TIM7CTLA
        jmp     @slot1

@s0sample:
        lda     _pcm_ptr
        cmp     _pcm_head
        bne     @s0feed
        lda     _pcm_ptr+1
        cmp     _pcm_head+1
        bne     @s0feed
        lda     _pcm_done
        bne     @s0finish
        inc     PCM_UNDERRUN     ; underrun: hold the last DAC value
        bne     :+
        dec     PCM_UNDERRUN     ; saturate at $FF; zero always means clean
:
        bra     @slot1
@s0finish:
        stz     TIM7CTLA
        stz     _dac_mode
        ldx     _dac_off
        stz     AUD0DAC,x
        bra     @slot1
@s0feed:
        lda     _dac_muted
        bne     @s0zero
        lda     (_pcm_ptr)
        bra     @s0write
@s0zero:
        lda     #0
@s0write:
        ldx     _dac_off
        sta     AUD0DAC,x
        ldx     #0
        jsr     @sample_advance
        bra     @slot1

@s0wave:
        ldy     wav_pos
        lda     (wav_ptr0),y
        ldx     _dac_muted
        beq     :+
        lda     #0
:       ldx     _dac_off
        sta     AUD0DAC,x
        tya
        clc
        adc     wav_step
        and     #$1F
        sta     wav_pos

@slot1:
        lda     INTSET
        and     #$20            ; timer 5 -> DAC slot 1
        bne     @slot1active
        jmp     @vbl
@slot1active:
        sta     INTRST
        lda     _dac_mode+1
        cmp     #1
        beq     @s1sample
        cmp     #2
        beq     @s1wave
        stz     TIM5CTLA
        jmp     @vbl

@s1sample:
        lda     _pcm_ptr+2
        cmp     _pcm_head+2
        bne     @s1feed
        lda     _pcm_ptr+3
        cmp     _pcm_head+3
        bne     @s1feed
        lda     _pcm_done+1
        bne     @s1finish
        inc     PCM_UNDERRUN+1
        bne     :+
        dec     PCM_UNDERRUN+1   ; saturate at $FF; zero always means clean
:
        jmp     @vbl
@s1finish:
        stz     TIM5CTLA
        stz     _dac_mode+1
        ldx     _dac_off+1
        stz     AUD0DAC,x
        jmp     @vbl
@s1feed:
        lda     _dac_muted+1
        bne     @s1zero
        lda     (_pcm_ptr+2)
        bra     @s1write
@s1zero:
        lda     #0
@s1write:
        ldx     _dac_off+1
        sta     AUD0DAC,x
        ldx     #2
        jsr     @sample_advance
        bra     @vbl

@s1wave:
        ldy     wav_pos+1
        lda     (wav_ptr1),y
        ldx     _dac_muted+1
        beq     :+
        lda     #0
:       ldx     _dac_off+1
        sta     AUD0DAC,x
        tya
        clc
        adc     wav_step+1
        and     #$1F
        sta     wav_pos+1
        bra     @vbl

; Advance one KIT source cursor. _dac_step is 1-4 for normal through 4x;
; zero is the half-rate sentinel and repeats each source byte once. X is the
; pointer-byte offset (0 for slot 0, 2 for slot 1). Walk at most four bytes
; and clamp on the published head so odd strides and live rate changes cannot
; leap over the end of available/sample data.
@sample_advance:
        txa
        lsr     a
        tay                             ; slot 0/1
        lda     _dac_step,y
        bne     @advance_add
        lda     _dac_phase,y
        eor     #1
        sta     _dac_phase,y
        bne     @advance_done
        inc     a                       ; second half-rate tick advances one
@advance_add:
        pha
        dec     a
        beq     @advance_fast           ; one byte can never leap over head
        lda     _pcm_head,x
        sec
        sbc     _pcm_ptr,x              ; low-byte forward distance
        cmp     #5
        bcs     @advance_fast           ; head cannot be crossed this tick
        pla
        tay
@advance_byte:
        inc     _pcm_ptr,x
        bne     :+
        lda     _pcm_ptr+1,x
        eor     #1
        sta     _pcm_ptr+1,x
:       lda     _pcm_ptr,x
        cmp     _pcm_head,x
        bne     :+
        lda     _pcm_ptr+1,x
        cmp     _pcm_head+1,x
        beq     @advance_done
:       dey
        bne     @advance_byte
@advance_done:
        rts
@advance_fast:
        pla
        clc
        adc     _pcm_ptr,x
        sta     _pcm_ptr,x
        bcc     @advance_done
        lda     _pcm_ptr+1,x
        eor     #1
        sta     _pcm_ptr+1,x
        rts

@vbl:
        lda     INTSET
        and     #$04            ; timer 2: VBlank
        beq     @out
        sta     INTRST
        inc     _frames
        bne     :+
        inc     _frames+1
:       lda     in_tick
        bne     @out
        inc     in_tick
        ldx     #zpspace-1
@save:  lda     sp,x
        sta     zpbuf,x
        dex
        bpl     @save
        cli                     ; let DAC IRQs nest during the C engine tick
        jsr     _engine_tick
        sei
        ldx     #zpspace-1
@rest:  lda     zpbuf,x
        sta     sp,x
        dex
        bpl     @rest
        stz     in_tick
@out:
        ply
        plx
        pla
        rti
