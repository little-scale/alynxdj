; cc65 2.18 lynx.cfg marks LOWCODE optional, but lynx/defdir.s imports
; __LOWCODE_SIZE__ unconditionally — an empty declaration instantiates the
; segment so the link resolves.
        .segment "LOWCODE"
