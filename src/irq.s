; Interrupt core: VBlank (timer 2) frame counter + PCM DAC feed (timer 7).
;
; The PCM feed is the jitter-critical path (DESIGN.md D2/D5): one sample
; byte from RAM into channel D's OUTPUT register per timer-7 IRQ (~7.8 kHz,
; ~45 cycles/IRQ ~= 9% CPU). The handler owns only A and the APPZP pointer;
; it must never touch cc65 zeropage state. Engine code must leave channel D
; alone while a sample runs (KIT voices set env off, dirty 0).

        .export         _vbl_install
        .export         _frames
        .export         _pcm_stop
        .export         _wave_start
        .export         _wave_rate
        .export         _wave_stop
        .export         _pcm_ring_start
        .export         _pcm_ptr
        .export         _pcm_head
        .export         _pcm_done
        .export         _pcm_peak
        .export         _wav_peak
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
AUD2DAC  := $FD32               ; channel C OUTPUT (wavetable DAC)
AUD2CTL  := $FD35
AUD3DAC  := $FD3A               ; channel D OUTPUT (sample DAC)
AUD3CTL  := $FD3D

PCM_BKUP = 127                  ; 1us clock -> 7812.5 Hz
PCM_CTLA = $98                  ; int enable | count | reload | 1us
WAVES_OFF = 7424                ; offsetof(struct songdata, waves)
RING_HI  = $D0                  ; 1 KB sample ring at $D000-$D3FF
RING_END = $D4

        .segment "APPZP" : zeropage
_pcm_ptr: .res 2                 ; ring tail — owned by the IRQ
wav_ptr: .res 2                 ; base of the active 32-byte wavetable

        .bss
_pcm_head: .res 2               ; ring head — owned by the C pump
_pcm_done: .res 1               ; pump: no more stream bytes coming
_pcm_peak: .res 1               ; per-frame peak |sample| on channel D (meter)
_wav_peak: .res 1               ; per-frame peak |entry| on channel C (meter)
in_tick: .res 1                 ; VBL tick re-entrancy guard
zpbuf:   .res 32                ; cc65 runtime zp save (tick runs C in IRQ)
wav_pos: .res 1                 ; table read position (wraps & $1F)
wav_step: .res 1                ; entries per IRQ (pitch step-doubling)
_frames: .res 2                 ; u16 frame counter (read by C)

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
        lda     #$9F            ; TIM2: int enable | reload | count | linked
        sta     TIM2CTLA
        cli
        rts

; void pcm_ring_start(void);  tail to ring base, start the feed IRQ
_pcm_ring_start:
        stz     TIM7CTLA
        stz     AUD3CTL         ; channel D shifter off: OUTPUT is ours
        stz     _pcm_ptr
        lda     #RING_HI
        sta     _pcm_ptr+1
        lda     #PCM_BKUP
        sta     TIM7BKUP
        lda     #PCM_CTLA
        sta     TIM7CTLA
        rts

; void pcm_stop(void);
_pcm_stop:
        stz     TIM7CTLA
        stz     AUD3DAC
        rts

; void __fastcall__ wave_start(unsigned char w);  point at sd.waves[w]
_wave_start:
        stz     TIM5CTLA        ; feed off while retargeting
        stz     AUD2CTL         ; channel C shifter off: OUTPUT is ours
        ldx     #0
        stx     wav_pos
        ; wav_ptr = _sd + WAVES_OFF + w*32
        sta     tmp_w
        lda     #<(_sd + WAVES_OFF)
        sta     wav_ptr
        lda     #>(_sd + WAVES_OFF)
        sta     wav_ptr+1
        lda     tmp_w
        asl     a               ; *32 (w <= 7 so no carry out of 8 bits)
        asl     a
        asl     a
        asl     a
        asl     a
        clc
        adc     wav_ptr
        sta     wav_ptr
        bcc     :+
        inc     wav_ptr+1
:       rts
tmp_w:  .byte   0

; void wave_rate(clock, bkup, step)  (cdecl-ish: step in A, others pushed)
_wave_rate:
        sta     wav_step        ; step (last arg, fastcall)
        jsr     popa            ; bkup
        sta     TIM5BKUP
        jsr     popa            ; clock
        ora     #$98            ; int enable | count | reload | clock
        sta     TIM5CTLA
        rts

; void wave_stop(void);
_wave_stop:
        stz     TIM5CTLA
        stz     AUD2DAC
        rts

handler:
        pha
        lda     INTSET
        and     #$80            ; timer 7: PCM byte due
        beq     @wave
        sta     INTRST          ; ack
        lda     _pcm_ptr         ; ring empty?
        cmp     _pcm_head
        bne     @feed
        lda     _pcm_ptr+1
        cmp     _pcm_head+1
        bne     @feed
        lda     _pcm_done       ; empty: done -> stop, else hold (underrun)
        beq     @wave
        stz     TIM7CTLA
        bra     @wave
@feed:  lda     (_pcm_ptr)
        sta     AUD3DAC
        bpl     @pp             ; peak-hold |sample| for the meter
        eor     #$ff
@pp:    cmp     _pcm_peak
        bcc     @pd
        sta     _pcm_peak
@pd:    inc     _pcm_ptr
        bne     @wave
        lda     _pcm_ptr+1
        inc     a
        cmp     #RING_END
        bne     :+
        lda     #RING_HI
:       sta     _pcm_ptr+1
@wave:
        lda     INTSET
        and     #$20            ; timer 5: wavetable entry due
        beq     @vbl
        sta     INTRST
        phy
        ldy     wav_pos
        lda     (wav_ptr),y
        sta     AUD2DAC
        bpl     @wp             ; peak-hold |entry| for the meter
        eor     #$ff
@wp:    cmp     _wav_peak
        bcc     @wd
        sta     _wav_peak
@wd:    tya
        clc
        adc     wav_step
        and     #$1F            ; 32-entry loop
        sta     wav_pos
        ply
@vbl:
        lda     INTSET
        and     #$04            ; timer 2: VBlank
        beq     @out
        sta     INTRST
        inc     _frames
        bne     :+
        inc     _frames+1
:       lda     in_tick         ; engine tick, re-entrancy guarded
        bne     @out
        inc     in_tick
        phx
        phy
        ldx     #zpspace-1  ; the cc65 runtime zeropage block
@save:  lda     sp,x
        sta     zpbuf,x
        dex
        bpl     @save
        cli                     ; let PCM/wave IRQs nest during the tick
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
