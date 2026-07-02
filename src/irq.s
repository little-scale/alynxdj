; Interrupt core: VBlank (timer 2) frame counter + PCM DAC feed (timer 7).
;
; The PCM feed is the jitter-critical path (DESIGN.md D2/D5): one sample
; byte from RAM into channel D's OUTPUT register per timer-7 IRQ (~7.8 kHz,
; ~45 cycles/IRQ ~= 9% CPU). The handler owns only A and the APPZP pointer;
; it must never touch cc65 zeropage state. Engine code must leave channel D
; alone while a sample runs (KIT voices set env off, dirty 0).

        .export         _vbl_install
        .export         _frames
        .export         _pcm_play
        .export         _pcm_stop
        .import         _kit_dir

INTRST   := $FD80
INTSET   := $FD81
TIM2CTLA := $FD09
TIM7BKUP := $FD1C
TIM7CTLA := $FD1D
AUD3DAC  := $FD3A               ; channel D OUTPUT (direct 8-bit DAC)
AUD3CTL  := $FD3D

PCM_BKUP = 127                  ; 1us clock -> 7812.5 Hz
PCM_CTLA = $98                  ; int enable | count | reload | 1us

        .segment "APPZP" : zeropage
pcm_ptr: .res 2                 ; owned by the IRQ while timer 7 runs

        .bss
pcm_end: .res 2
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

; void __fastcall__ pcm_play(unsigned char slot);  slot 0-7
_pcm_play:
        stz     TIM7CTLA        ; stop the feed while retargeting
        stz     AUD3CTL         ; channel D shifter off: OUTPUT is ours
        asl     a
        asl     a
        tax                     ; dir entry: {ptr.w, len.w} * slot
        lda     _kit_dir,x
        sta     pcm_ptr
        clc
        adc     _kit_dir+2,x
        sta     pcm_end
        lda     _kit_dir+1,x
        sta     pcm_ptr+1
        adc     _kit_dir+3,x
        sta     pcm_end+1
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

handler:
        pha
        lda     INTSET
        and     #$80            ; timer 7: PCM byte due
        beq     @vbl
        sta     INTRST          ; ack
        lda     (pcm_ptr)
        sta     AUD3DAC
        inc     pcm_ptr
        bne     :+
        inc     pcm_ptr+1
:       lda     pcm_ptr
        cmp     pcm_end
        lda     pcm_ptr+1
        sbc     pcm_end+1
        bcc     @vbl            ; ptr < end: keep feeding
        stz     TIM7CTLA        ; sample done
@vbl:
        lda     INTSET
        and     #$04            ; timer 2: VBlank
        beq     @out
        sta     INTRST
        inc     _frames
        bne     @out
        inc     _frames+1
@out:
        pla
        rti
