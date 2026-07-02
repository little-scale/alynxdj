; VBlank interrupt: timer 2 IRQ -> frame counter.
;
; The handler owns nothing but A; it must never touch cc65 zeropage state
; (the sibling rule: the jitter-critical IRQ path stays isolated). Future
; milestones chain the engine tick here.

        .export         _vbl_install
        .export         _frames

INTRST  := $FD80
TIM2CTLA := $FD09

        .bss
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
        lda     #$9F            ; TIM2: int enable | reload | count | linked clock
        sta     TIM2CTLA
        cli
        rts

handler:
        pha
        lda     #$04            ; ack timer 2
        sta     INTRST
        inc     _frames
        bne     :+
        inc     _frames+1
:       pla
        rti
