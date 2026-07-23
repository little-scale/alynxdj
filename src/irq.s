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
        .export         _dac_rate_set
        .export         _wave_start
        .export         _wave_rate
        .export         _wave_stop
        .export         _pcm_ring_start
        .export         _pcm_ptr
        .export         _pcm_head
        .export         _pcm_done
        .export         _dac_mode
        .export         _dac_off
        .export         _dac_muted
        .export         _dac_rate
        .export         _dac_peak
        .import         _sd
        .import         _engine_tick
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
PCM_UNDERRUN := $C027             ; two wrapping diagnostic counters

PCM_CTLA = $98                  ; int enable | count | reload | 1us
WAVES_OFF = 7424                ; offsetof(struct songdata, waves)
RING0_HI = $D0                  ; slot 0: $D000-$D1FF (512 bytes)
RING1_HI = $D2                  ; slot 1: $D200-$D3FF (512 bytes)

        .segment "APPZP" : zeropage
_pcm_ptr: .res 4                ; two ring tails, IRQ-owned
wav_ptr0: .res 2
wav_ptr1: .res 2

        .bss
_pcm_head: .res 4               ; two ring heads, pump-owned
_pcm_done: .res 2
_dac_mode: .res 2               ; DAC_NONE / DAC_SAMPLE / DAC_WAVE
_dac_off: .res 2                ; owning channel * 8
_dac_muted: .res 2              ; consume normally, write zero when muted
_dac_rate: .res 2               ; KIT timer reload (default 127)
_dac_peak: .res 2               ; per-frame |OUTPUT| peak, per slot
in_tick:  .res 1                ; VBL tick re-entrancy guard
zpbuf:    .res 32               ; cc65 runtime zp save (tick runs C in IRQ)
wav_pos:  .res 2
wav_step: .res 2
tmp_slot: .res 1
tmp_w:    .res 1
tmp_rate: .res 1
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
        cmp     #0
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
        lda     _dac_rate
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
        lda     _dac_rate+1
        sta     TIM5BKUP
        lda     #PCM_CTLA
        sta     TIM5CTLA
        rts

; void __fastcall__ dac_rate_set(unsigned char slot, unsigned char rate);
; Store even before a deferred cart trigger starts, so same-row S survives.
_dac_rate_set:
        sta     tmp_rate
        jsr     popa
        tax
        lda     tmp_rate
        sta     _dac_rate,x
        lda     _dac_mode,x
        beq     @done
        cpx     #0
        bne     @one
        lda     tmp_rate
        sta     TIM7BKUP
        rts
@one:   lda     tmp_rate
        sta     TIM5BKUP
@done:  rts

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
        lda     INTSET
        and     #$10            ; timer 4 -> MIDI UART receive
        beq     @slot0check
        lda     SERCTL
        and     #$40            ; level IRQ can outlive RX ready by a cycle
        beq     @serialspurious
        phx
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
        plx
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
        plx
        bra     @slot0check
@serialspurious:
        lda     #$10
        sta     INTRST
@slot0check:
        lda     INTSET
        and     #$80            ; timer 7 -> DAC slot 0
        bne     @slot0
        jmp     @slot1
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
        bra     @slot1
@s0finish:
        stz     TIM7CTLA
        stz     _dac_mode
        phx
        ldx     _dac_off
        stz     AUD0DAC,x
        plx
        bra     @slot1
@s0feed:
        phx
        phy
        lda     (_pcm_ptr)
        tay
.ifndef ALYNXDJ_NO_DAC_PEAKS
        bpl     :+
        eor     #$ff
:       cmp     _dac_peak
        bcc     :+
        sta     _dac_peak
:
.endif
        lda     _dac_muted
        bne     :+
        tya
        bra     :++
:       lda     #0
:       ldx     _dac_off
        sta     AUD0DAC,x
        inc     _pcm_ptr
        bne     @s0done
        inc     _pcm_ptr+1
        lda     _pcm_ptr+1
        cmp     #RING1_HI
        bne     @s0done
        lda     #RING0_HI
        sta     _pcm_ptr+1
@s0done:
        ply
        plx
        bra     @slot1

@s0wave:
        phx
        phy
        ldy     wav_pos
        lda     (wav_ptr0),y
.ifndef ALYNXDJ_NO_DAC_PEAKS
        pha
        bpl     :+
        eor     #$ff
:       cmp     _dac_peak
        bcc     :+
        sta     _dac_peak
:       pla
.endif
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
        ply
        plx

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
        jmp     @vbl
@s1finish:
        stz     TIM5CTLA
        stz     _dac_mode+1
        phx
        ldx     _dac_off+1
        stz     AUD0DAC,x
        plx
        bra     @vbl
@s1feed:
        phx
        phy
        lda     (_pcm_ptr+2)
        tay
.ifndef ALYNXDJ_NO_DAC_PEAKS
        bpl     :+
        eor     #$ff
:       cmp     _dac_peak+1
        bcc     :+
        sta     _dac_peak+1
:
.endif
        lda     _dac_muted+1
        bne     :+
        tya
        bra     :++
:       lda     #0
:       ldx     _dac_off+1
        sta     AUD0DAC,x
        inc     _pcm_ptr+2
        bne     @s1done
        inc     _pcm_ptr+3
        lda     _pcm_ptr+3
        cmp     #$D4
        bne     @s1done
        lda     #RING1_HI
        sta     _pcm_ptr+3
@s1done:
        ply
        plx
        bra     @vbl

@s1wave:
        phx
        phy
        ldy     wav_pos+1
        lda     (wav_ptr1),y
.ifndef ALYNXDJ_NO_DAC_PEAKS
        pha
        bpl     :+
        eor     #$ff
:       cmp     _dac_peak+1
        bcc     :+
        sta     _dac_peak+1
:       pla
.endif
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
        ply
        plx

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
        phx
        phy
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
        ply
        plx
        stz     in_tick
@out:
        pla
        rti
