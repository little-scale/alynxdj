/* Global Mikey mixer/stereo initialization. */
#include <lynx.h>

#pragma code-name (push, "MIDICODE")

void sound_init(void)
{
    /* crt0 muted everything: MSTEREO=$FF (all channel/side disables set),
       volumes 0. Open the gates; keep volumes per-note. */
    MIKEY.mstereo = 0x00;
    MIKEY.attena = 0xFF;
    MIKEY.attenb = 0xFF;
    MIKEY.attenc = 0xFF;
    MIKEY.attend = 0xFF;
    MIKEY.panning = 0xFF;   /* attenuation active on all channels (D8:
                               write-always; Lynx I ignores, Lynx II pans) */
}

#pragma code-name (pop)
