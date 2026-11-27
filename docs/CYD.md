# The 2.8" ESP32-S3 CYD (ES3C28P)

The tank on a stock "cheap yellow display": the ES3C28P from QDtech, an
ESP32-S3 behind a 2.8-inch IPS panel with capacitive touch. No modifications
to the board - everything here uses it as it comes. The vendor's
specification: [ES3C28P_ES2N28P_Specification_V1.0.pdf](https://www.lcdwiki.com/res/ES3C28P/ES3C28P_ES2N28P_Specification_V1.0.pdf).

## The board, against the Waveshare AMOLED

| | Waveshare 1.8" AMOLED | ES3C28P CYD |
|---|---|---|
| Chip | ESP32-S3, 16 MB flash, 8 MB octal PSRAM | the same (40 MHz crystal) |
| Screen | SH8601 / CO5300 AMOLED over QSPI, 368 x 448, 322 ppi | ILI9341V IPS over 4-wire SPI, 240 x 320, 143 ppi |
| Touch | FT3168 / CST816 | FT6336G (the FT5x06 family, I2C 0x38) |
| Audio | ES8311 + NS4150B | ES8311 (I2C 0x18) + an amp enabled low on GPIO1 |
| Power | AXP2101 PMIC, fuel gauge, PWR key | a charger for a LiPo on its socket; the cell's voltage on GPIO9; no PMIC |
| IMU | QMI8658 (I2C 0x6B), on the board | none on the board: a QMI8658C or an MPU-6050 (0x68) on the I2C socket, SDA IO16 / SCL IO15, whichever answers at boot - an MPU-6050 now (see *The IMU*) |
| Gestures | upside-down flip, handling (codec warm, the light's idle rule); the PWR key sleeps | the same, plus face down sleeps and face up wakes; BOOT is the sleep key (see *Gestures*) |
| Clock | PCF85063 RTC | none |

Pins: `firmware/main/board_pins.h`, from section 4.2 of the specification.
The LCD resets with the chip (CHIP_PU), so there is no reset pin to drive.

## Building and flashing

```sh
tools/build_cyd.sh                                     # build only
tools/build_cyd.sh <port> --model                      # the first time: app + the model partition
tools/build_cyd.sh <port>                              # after that: the app
```

`<port>` is `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00`,
never a `/dev/ttyACM<n>`, whose numbers shuffle between plug-ins. The script
uses ESP-IDF (`IDF_PATH`, or `~/esp/esp-idf`; 5.4 or later), its own build
directory (`firmware/build_cyd`) and its own sdkconfig - `sdkconfig.defaults`
with `sdkconfig.cyd` on top - so the AMOLED build is untouched.
The defaults only fill in what an sdkconfig lacks, so a changed default would
never reach the existing one; the script starts it afresh whenever a defaults
file is newer.

Back up the factory image before the first flash; the board then goes back
to how it arrived with one `write_flash`:

```sh
esptool --chip esp32s3 -p <port> -b 921600 read_flash 0 0x1000000 factory_16MB.bin
```

## What the port changes

**Since the 0.4.3 merge (2026-11) the CYD runs the rectangle's own 448 x 368
tank, scaled to its glass.** Upstream grew from 21 pages to 80 between 0.3.3
and 0.4.3 and none of the new ones are laid out for 320 x 240, so the fork no
longer keeps a layout of its own: nothing in `common/` knows this board. The
display port shows the frame scaled down, and a touch is turned back into the
frame. A new upstream page needs no work here.

- **The scale** (`firmware/main/qvga_scale.h`): 7 frame px across for 5 on the
  glass, 23 down for 15, so the whole frame fills the whole glass and nothing
  is cropped (the picture stands 9.5% wider than on the 1.8). The filter is
  the exact area average, so a 1 px stroke of the small font thins to a grey
  instead of vanishing. Small text is softer than it was on the native
  layout; that is the price.
- **Touch** comes back through the same scale
  (`display_port_panel_to_tank`, `touch_port_ft3168.c` `map_touch`), then
  through upstream's own path, so the ADVANCED page's touch calibration works.
- **Its own board id**: `PT_BOARD` is `cyd28` (the Touch-LCD-2: `tlcd2`),
  `common/version.h`. Upstream publishes no manifest for them, so CHECK FOR
  UPDATES finds nothing, and an image marked for another board is refused.
  Before this the images carried the 1.8's id.
- **Old saves** from the 320 x 240 build (0.3.3 and before) carry no world
  size. On these two builds such a save is taken as 320 x 240 and moved in
  proportion at load (`common/progression.c` `relayout`): floor spots keep
  their place along the floor, the algae film is resampled onto the larger
  grid, and the snail starts again.
- **The settings rows are gone.** SCREEN (UPRIGHT / FLIPPED) and FACE DOWN
  (SLEEP / IGNORE) were rows the fork added to the settings page. The page is
  upstream's now, so both moved to the serial console and are still kept in
  NVS (`firmware/main/orientation.c`): `wayup flip|upright` and
  `facedown on|off` (`tools/director.py`). Upstream's own ROTATION lock is on
  the page as on every board.
- **A preview without a board:** `make -C sim qvga_shot`, then
  `sim/qvga_shot in.ppm out.ppm` turns any simulator picture (`--snapshot`,
  `--page-shot`, ...) into what the glass shows, through the same header the
  firmware compiles. `make -C sim check-qvga` runs the scaler's checks.

What is the fork's own, all in `firmware/main/` plus three small hooks:
`display_port_spi.c` and `qvga_scale.h` (the panel and the scale),
`imu_mpu6050.c` (the stand-in IMU), `orientation.c`, the `Q_*` pins in
`board_pins.h`, and marked `(this fork)` blocks in `touch_port_ft3168.c`,
`imu_port_qmi8658.c`, `audio_port_es8311.c`, `main.c` and `director.c`.
Outside the firmware: the board ids in `version.h` and `tools/pt_boards.py`,
and the old-save rule in `progression.c`.

**Display:** the ILI9341 scans landscape through MADCTL (`swap_xy` and the
mirrors); the scaled frame goes out from two ping-pong DMA stripes of 20
glass rows, in the panel's byte order; 40 MHz SPI. The backlight is LEDC PWM
on GPIO45; the flip is the same register with both mirrors toggled. The
boards do not stand on a side: only upright and turned over.

**Audio:** the same ES8311 on the CYD's own I2S pins. The pins and the level
that turns the amplifier on are the board's (`board_pins.h`): the CYD's is
enabled low on GPIO1, so the port sets it off before the pin becomes an
output and holds it high through deep sleep. No PMIC switches the codec's
analog supply here; the port's call to do so finds none and does nothing.

**Clock:** there is no RTC chip. ESP-IDF's system time runs on through deep
sleep on the chip's own RTC timer, so with no chip answering, a plausible
system time is kept and only a power-on is seeded from the build time.

**Touch:** the FT5x06 driver with the reset on GPIO18. On the bench the panel
reads turned 180 degrees from the picture, and the AMOLED's 10 px
finger-landing correction made every miss land above its button, so it is
0 here.

Sixteen pages as the glass shows them, through the board's own filter
(`sim/qvga_shot`), are in [cyd/0.4.3-scaled-pages.png](cyd/0.4.3-scaled-pages.png).
Headings, buttons, the stats card, the shop, the keyboards and the Wi-Fi list
read cleanly. The smallest captions (the settings page's ROTATION and
GRAPHICS labels, the version line, "120 OF 120 COLLECTED") turn to grey
smudges: they are 1x text in a 448 px layout, about 4 px tall here.

**Not yet run on a board.** The 0.4.3 port was built and checked on the host
only (2026-11-27): the three images compile, the simulator's selftests pass
at 448 x 368, and the scaler passes its own checks. Frame rate, text
legibility on the real glass, touch accuracy and the save migration on a real
save are still to be seen on the bench. Expect a lower frame rate than the
figures below: the tank now draws 2.1 times the pixels, and each frame is
scaled before it is sent.

## Measured (2026-09-26, the first board, the native 320 x 240 build)

- 17.5 to 21 fps at 40 MHz SPI: 5 to 14 ms to render a frame (the scene
  decides), 30 ms to send it.
- **80 MHz does not work on this panel.** The firmware sent a frame in 16 ms
  (29 to 32 fps), but the glass showed stripes and no picture; back at 40
  it is right again. The bus has no step between the two: the clock divides
  an 80 MHz source.
- The model: 12.7 tokens a second, 3.5 s a decision - as on the AMOLED board.
- Sound, on a speaker on the board's socket: the codec and amplifier up
  255 ms after the first touch, down 5 s after the last sound; 17 of the 24
  cues are in the bank.
- Sleep on BOOT: a short press darkens the tank and light-sleeps it; a press
  within 20 min resumes in place (a 6 s nap, the fish where they were). Past
  that it deep-sleeps, and BOOT - or the director's timer - wakes it with a
  boot that puts the fish back and lives the time through (240 s asleep came
  back as 0.1 h).
- On the I2C bus: 0x18 (the ES8311), 0x38 (the FT6336) and, with the
  breakout fitted, 0x68 (the MPU-6050). The AMOLED's other parts are absent
  and say so at boot: no AXP2101, no QMI8658, no PCF85063.

## The IMU

The firmware's IMU work is all accelerometer, polled four times a second: the
180-degree flip (held while the board lies flat or stands on its side, so it
never flaps), and the handling detector (`moving` keeps the codec warm,
`handled` counts as touching the tank for the light's idle rule). No gyro, no
tap engine, no interrupt, so no INT wire is needed.

**The QMI8658 is always probed first; the MPU-6050 is a Kconfig line, on for
the CYD** (`sdkconfig.cyd`: `POCKET_TANK_IMU_MPU6050`). At boot
`imu_port_init` asks for a QMI8658 and, when none answers, for the MPU-6050,
so the QMI8658C takes over by itself when it is fitted. The logic is one
copy, upstream's `imu_port_qmi8658.c`; the MPU-6050 is a small driver beside
it (`imu_mpu6050.c`) that hands over its samples and its mounting.

### Supported IMUs, and wiring one

Either of these, on the CYD's I2C socket. The firmware probes the enabled
ones at boot, in this order, and uses the first that answers:

| chip | I2C address | WHO_AM_I | Kconfig line (CYD default) | notes |
|---|---|---|---|---|
| QMI8658 / QMI8658C | 0x6B, or 0x6A | 0x05 | always probed first | the AMOLED board's chip; on the CYD its axes need one bench reading (*Still open*) |
| MPU-6050 (GY-521 and the like) | 0x68 (AD0 low), or 0x69 | 0x68; clone dies reporting 0x70, 0x71, 0x72, 0x73 or 0x98 are taken too | `POCKET_TANK_IMU_MPU6050` (y) | what is fitted now; axes measured, in `sdkconfig.cyd` |

Anything else on the bus is ignored; with neither answering, the boot log
says so, and `wayup flip|upright` on the console sets the way up.

| IMU breakout | CYD | why |
|---|---|---|
| VCC | **3V3** | never 5 V: GPIO15 and GPIO16 are not 5 V tolerant, and some breakouts pull SDA and SCL up to their VCC |
| GND | GND | |
| SDA | **IO16** | the board's I2C bus, shared with the touch (0x38) and the codec (0x18), at 400 kHz |
| SCL | **IO15** | |
| INT | not connected | nothing polls it; a deep-sleep wake on movement would need it (*Still open*) |
| AD0 / SA0 | as the breakout has it | picks between the two addresses above; both are probed |
| XDA, XCL (MPU-6050) | not connected | its auxiliary bus, unused |

The socket's pin order is not in the board's specification - read it off the
silkscreen, or meter it against the touch controller's lines. No pull-up
resistors were needed for the MPU-6050: the bus enables the chip's internal
ones, and the GY-521 carries its own 4.7 k. A bare QMI8658C with none of its
own may want a pair - check its breakout. **Mount it flat against the CYD's back**, so its Z axis
is the one out of the glass; the up axis and both signs then come from one
upright and one flat reading with the director's `imu`. Proof it is wired:
the boot log's bus scan lists its address (`i2c: device at 0x68`), and
`imu: MPU-6050 up at 0x68` (or `QMI8658 up at ...`) follows.

**The MPU-6050**, a GY-521-style breakout:

- wired as in the table above; it answers at 0x68 (AD0 low).
- accel only at +-2 g, the QMI8658's scale (16384 counts per g), so every
  threshold carries over; a 10 Hz low-pass keeps a still table at a motion
  count of 6-92 against the 220 threshold; gyros in standby; its sleep bit for
  the drowse bracket.
- **mounted flat against the CYD's back, its pins toward the top edge.** The
  axes in `sdkconfig.cyd` are for that mounting, measured 2026-09-30:
  upright reads X at -0.95 g, flat on its back reads Z (out of the glass). A
  different mounting is one bench reading: stand the board upright, run the
  director's `imu`, and the axis carrying ~16000 counts, with its sign, is
  `POCKET_TANK_IMU_MPU6050_UP_AXIS` / `_UP_NEGATIVE`.
- this breakout's Z reads about 0.79 g at rest. The flip never uses Z.

Measured on the bench with the director's `imu` (2026-09-30): upside down
flipped the picture 0.6 s later, back upright flipped it back, on its side in
either direction changed nothing, and a pick-up read MOVING.

**Face down sleeps the tank** (`POCKET_TANK_IMU_FACE_DOWN_SLEEP`, on for the
CYD). Screen down, level and still for 2 s is a short press of the sleep key:
the tank saves, darkens and light-sleeps. "Still" here is a motion count
under 1000 a poll, not the handling detector's 220: a hand steadying the
board reads 200-800, which kept the gesture from firing on the bench. **Any
sleep that starts face down wakes when it is turned face up** - the
gesture's, or BOOT pressed while it lies there; a BOOT sleep face up keeps
BOOT as its only wake. For those the IMU stays awake through the 20-minute
grace - there are no rails to cycle on the CYD - and each 1 s wake of the
grace reads it once: no longer face down (turned up, or picked up) resumes
in place, as BOOT does. No answer from the IMU keeps it asleep. After
the grace the IMU sleeps and the board deep-sleeps; only BOOT wakes it then
(motion could only with the INT wire). The gesture fires once per lie-down,
so waking it with BOOT while it still lies face down does not put it straight
back to sleep. Face down means the axis out of the glass reads more than
0.5 g toward the table with both in-screen axes under 0.35 g; the sign that
axis reads screen-up is `POCKET_TANK_IMU_MPU6050_OUT_NEGATIVE` (y for the
mounting above: flat, screen up, Z reads -0.79 g; screen down, +1.23 g).

Bench, 2026-09-30, the breakout held flat against the back: face down slept
the tank and face up woke it within a second; BOOT woke it and slept it again
while it lay face down, and that BOOT sleep woke on face up too.

**The gesture's switch is `facedown on|off` on the serial console** (SLEEP by
default, kept in NVS as `tank/facedn`). Until the 0.4.3 merge it was a row on
the settings page; see *What the port changes*.

### Gestures

How each one is told apart. Every movement reading is the IMU polled at 4 Hz
by `imu_port_poll` (`firmware/main/imu_port_qmi8658.c`), in counts at +-2 g (16384 a
g); the README's *Gestures* table is the keeper's version of this.

| gesture | detected as | effect | where |
|---|---|---|---|
| upside down | the up axis past 0.21 g the other way, and dominant over the other in-screen axis, for 3 polls (~0.75 s) | picture and touch turn 180 degrees | `imu_port.c`, applied per frame in `main.c` |
| flat, or on its side | the up axis not dominant | nothing: the last orientation holds | the same vote |
| picked up, carried | one poll's summed change over 220 (~0.013 g): `moving`, held 1 s | the codec stays warm | `main.c`, `audio_port_prewarm` |
| held | `moving` on two polls in a row: `handled` | counts as attention for the light's idle rule (AUTO) | `tank_handled` |
| screen down, level, still, 2 s (CYD) | out-of-glass axis over 0.5 g toward the table, in-screen axes under 0.35 g, motion under 1000, 8 polls; once per lie-down | sleeps as a BOOT press | `imu_port_take_face_down`, `main.c` |
| screen up / picked up, asleep (CYD) | one read each 1 s slice of the grace: no longer face down | wakes in place - after a sleep that began face down | `imu_port_face_down_now`, `enter_sleep_for` |
| BOOT, short press | the button (GPIO0), at release | sleep; within the 20-minute grace a press wakes in place; after it, deep sleep, and BOOT boots | `sleep_button_poll`, `enter_sleep_for` |
| BOOT held + a tap | a touch landing while BOOT is down | the *Reset tank?* prompt | `sleep_button_poll` |
| double-tap the glass | two quick taps, then a pause (LIGHTS OUT = MANUAL, the default) | the tank light on / off, saved | `tank.c` (`light_manual_off`) |

The face-down rows need `POCKET_TANK_IMU_FACE_DOWN_SLEEP` (on for the CYD)
and `facedown on` (the default). Deep sleep after the grace hears only
BOOT: waking it on movement needs the IMU's INT line wired (below).

## Still open

- **The MPU-6050 is held, not mounted.** The axes in `sdkconfig.cyd`
  are for it flat against the back with its pins toward the top edge; solder
  or glue it that way, or re-read the axes (*The IMU*) for the mounting it
  gets. Its power LED draws 1-3 mA, which dominates deep sleep on a cell -
  lift it, or its resistor, if the cell's life matters.
- **The QMI8658C, when it arrives,** is probed first and takes over by
  itself, but its axes are then the 1.8's (`imu_port_qmi8658.c`: up = -Y, out
  of the glass = +Z). The per-board Kconfig axes the fork had for it did not
  survive the 0.4.3 merge: one upright and one flat reading with the
  director's `imu` are needed, and a `(this fork)` line in `imu_port_init`
  to set them for the CYD.
- **Waking from deep sleep on movement** needs the IMU's INT line on an RTC
  GPIO - GPIO21 or GPIO14 on the 4-pin expansion socket (not GPIO3, a
  strapping pin; never GPIO0) - an ext1 wake beside BOOT's ext0, and the
  chip's motion interrupt armed at sleep. Not done: within the 20-minute
  grace, face up already wakes it.
- **Two face-down sleeps stayed dark, BOOT included,** in the first hour of
  bring-up (2026-09-30) and never since, across every later round - BOOT and
  face up, in either order. Not explained. If one recurs, note the steps; the
  task watchdog armed across the sleep, for a backtrace, is the next
  instrument.

- **Battery.** The cell's voltage reaches GPIO9 through the board's divider,
  whose ratio is still to be measured. Without a meter the battery pill and
  its page stay hidden.
- **A power cut, or the RESET button, loses the time** (it resets the chip's
  RTC timer too): the clock starts again from the build time, and that
  absence is not lived through. Deep sleep keeps it.
- **Sending while drawing.** A frame is drawn and then sent, one after the
  other; sending it while the next one is drawn would lift the ceiling at
  40 MHz to about 33 fps.
- **Legibility on the glass.** The scaled 5x7 font is about 5 px tall on a
  143 ppi panel. The host preview reads; the real glass is still to be seen.
  The Wi-Fi keyboard is the page most at risk.
