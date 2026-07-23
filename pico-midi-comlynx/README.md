# ALYNXDJ USB-MIDI to ComLynx bridge

This firmware turns a Raspberry Pi Pico/RP2040 into a class-compliant USB-MIDI
device for a computer or DAW. It forwards MIDI channels 1–4 and MIDI real-time
transport to one Atari Lynx over ComLynx.

It is deliberately send-only and has no heartbeat or timeout. It does **not**
yet make the Pico a USB host for a keyboard plugged directly into the Pico.

## What reaches the Lynx

- Channel messages on MIDI channels 1–4 are emitted with a complete status
  byte. On the Lynx, select `OPTIONS > SYNC > MIDI`: the channels drive tracks
  A–D with instruments 01–04.
- The Pico counts MIDI Timing Clock (`F8`) and emits one row-rate `F8` after
  every six source clocks (24 PPQN / four tracker rows per quarter). Start
  (`FA`), Continue (`FB`) and Stop (`FC`) are forwarded. On the Lynx, select
  `IN24`: Start supplies a row-0 cue when the user has not already armed a
  local row, and each received `F8` grants one row.
  Continue currently also restarts at song row 0 because ALYNXDJ does not yet
  implement MIDI Song Position Pointer.
- System Reset (`FF`) is forwarded as the MIDI-takeover panic command.

The same firmware stream supports both Lynx modes; no switch on the Pico is
needed. `MIDI` ignores clock transport, while `IN24` ignores notes.

In `IN24`, cue a SONG row on the Lynx and start its transport first. ALYNXDJ
shows `WAIT`; the first row pulse starts that exact row and changes the label
to `PLAY`. A forwarded Start does not replace an already waiting local cue.

## Prototype wiring: one Pico, one Lynx

Default firmware output is `GP2`. Change it at configure time with
`-DCOMLYNX_TX_PIN=<gpio>`.

The Lynx uses a **2.5 mm TRS** plug. Looking at the metal contacts of the
plug—not at possibly inconsistent cable-wire colours—the pinout is:

| 2.5 mm TRS contact | ComLynx function | Connect to Pico |
|---|---|---|
| Tip | +5 V supplied by the Lynx | **No connection. Insulate this conductor.** |
| Ring | Shared DATA (combined transmit/receive) | 470 ohm resistor, then the protected GP2 node |
| Sleeve | Ground | Any Pico GND; physical pin 3 is adjacent to GP2 |

For a standard Raspberry Pi Pico/Pico H with the USB connector at the top:

| Pico signal | Physical header pin | Purpose here |
|---|---:|---|
| GND | 3 | ComLynx sleeve/ground |
| GP2 | 4 | Protected ComLynx DATA input/output |
| 3V3(OUT) | 36 | Schottky clamp rail only |

```text
2.5 mm TRS ring (DATA) ── 470 ohm ──+──── Pico GP2, physical pin 4
                                    |
                                    +───|>|─── Pico 3V3(OUT), pin 36
                                     low-Vf Schottky
                                   anode ─┘ └─ cathode/banded end

2.5 mm TRS sleeve (GND) ───────────── Pico GND, physical pin 3
2.5 mm TRS tip (+5 V) ─────────────── no connection; insulate it
```

The BAT54 or substitute Schottky anode goes to the GPIO/resistor node and its
cathode goes to Pico 3V3(OUT). On a leaded diode, the cathode is the banded
end. Firmware switches the GPIO only between output-low and high-impedance
input; it never drives high. Power the Pico from USB whenever the Lynx is
connected, because the clamp can otherwise feed a small current into an
unpowered 3.3 V rail.

### BAT54 substitutes available from Jaycar

Catalogue checked 23 July 2026; delivery and individual-store stock can
change. Keep the 470 ohm series resistor and the polarity shown above with
every substitute.

| Jaycar part | Suitability | Notes |
|---|---|---|
| [ZR1141 BAT46/BAT48, DO-35](https://www.jaycar.com.au/bat46-bat48-schottky-diode-do35g/p/ZR1141) | **Recommended through-hole substitute** | Small-signal Schottky and the closest easy-to-wire replacement. Jaycar lists a 0.45 V forward drop; BAT46 is specified at 0.45 V maximum even at 10 mA, above the roughly 2–3 mA expected here. |
| [ZR1020 1N5819, DO-41](https://www.jaycar.com.au/1n5819-schottky-diode-40v-1a-do41/p/ZR1020) | **Usable through-hole substitute** | A larger 1 A rectifier, but its forward drop is low at this circuit's few-milliamp clamp current and its extra capacitance is harmless at 62.5 kbaud. The catalogue's 0.6 V figure is at the part's much higher rated-current test point, not this operating point. |
| [ZR1021 SM5819A, DO-214AC](https://www.jaycar.com.au/smd-diode-sm5819a-schottky-40v-1a-pack-10/p/ZR1021) | **Usable SMD substitute** | The surface-mount counterpart to the 1N5819 option; electrically suitable, but less convenient for a hand-wired cable and currently listed as clearance. |
| [ZR1027 1N5711, DO-35](https://www.jaycar.com.au/diode-1n5711-schottky-do35/p/ZR1027) | **Not recommended without measurement** | It is a Schottky diode, but its specified forward drop is already 0.41 V at 1 mA and rises with current. That leaves less margin than BAT46 or 1N5819 when clamping a 3.3 V GPIO. |

The RP2040 absolute limit is the live IOVDD rail plus 0.5 V. Do not select a
replacement from its name alone: with the Pico powered from USB and the Lynx
connected, measure both 3V3(OUT) and the GP2 node in the released/high state.
Aim for GP2 no more than about 0.3 V above the measured rail (roughly 3.6 V),
and do not use an assembly that approaches 0.5 V above it (roughly 3.8 V).
The buffered interface described below remains the preferred finished design.

### Cable assembly checklist

1. With both devices unplugged, identify the pigtail conductors using a
   continuity meter against the plug's **tip, ring, and sleeve**. Do not trust
   wire colours.
2. Insulate the tip/+5 V conductor separately. It must not touch the Pico,
   ground, DATA, or the cable shield.
3. Connect sleeve to Pico GND physical pin 3.
4. Connect ring through 470 ohms to Pico GP2 physical pin 4.
5. Connect the BAT54 or documented substitute's anode to the GP2/resistor
   junction and its cathode/banded end to Pico 3V3(OUT) physical pin 36.
6. Before inserting the plug into the Lynx, confirm continuity from sleeve to
   Pico GND, approximately 470 ohms from ring to GP2, and no connection from
   tip to any Pico pin.
7. Power the Pico from USB first, then connect it to the Lynx. Never power the
   Pico from the ComLynx tip.

A 1N4148 is not a safe substitute: its higher forward voltage can place the
GPIO above the RP2040 maximum. Use BAT46/BAT48 or 1N5819 when BAT54 is not
available; do not assume that every diode sold as Schottky has enough margin.
A SN74LVC1G07 open-drain buffer is the preferred finished interface; a
BSS138/2N7002 or small NPN is another robust option but requires inversion
appropriate to that circuit.

On the first prototype, verify:

- released Pico GPIO node no higher than approximately 3.6 V;
- ComLynx low level below approximately 0.8 V;
- common ground and no connection to the ComLynx +5 V contact.

Pinout references:

- [Atari Lynx schematic archive](https://atariage.com/Lynx/archives/schematics/index.html)
- [Original ComLynx cable and connector measurements](https://atarilynxdeveloper.wordpress.com/2013/10/21/creating-a-comlynx-to-usb-cable/)
- [2.5 mm TRS contact mapping discussion](https://forums.atariage.com/topic/145622-is-it-possible-to-build-your-own-comlynx-cable/#findComment-1771979)
- [Official Raspberry Pi Pico board pinout](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html)
- [Official RP2040 GPIO absolute maximum ratings](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Vishay BAT46 electrical characteristics](https://www.vishay.com/docs/85662/bat46.pdf)

## Serial format

PIO emits the Lynx UART format directly at 62,500 baud:

```text
start 0 | eight data bits, LSB first | fixed Space ninth bit 0 | stop 1
```

The stop/high state is generated by releasing the GPIO. A 256-byte queue
absorbs USB packet bursts; if that queue ever overflows, the bridge discards
the pending software bytes and emits `FF` panic rather than leave a hanging
MIDI note.

## Build and flash

Install the Arm toolchain and clone the Pico SDK with its submodules, then:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S pico-midi-comlynx -B build/pico-midi-comlynx -DPICO_BOARD=pico
cmake --build build/pico-midi-comlynx
```

Hold BOOTSEL while connecting the Pico and copy
`build/pico-midi-comlynx/alynxdj_midi_comlynx.uf2` to the mounted `RPI-RP2`
drive. The computer should then expose **ALYNXDJ MIDI Bridge** as a MIDI
destination. Route the desired notes and MIDI Clock/transport to that port.

The descriptor uses TinyUSB's example VID for prototype development. Obtain a
proper USB VID/PID before distributing bridge hardware commercially.
