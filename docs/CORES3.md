# The M5Stack CoreS3

A fourth board for pocket-tank: the M5Stack CoreS3 (and the camera-less CoreS3 SE).
An ILI9342C IPS panel over 4-wire SPI, an FT6336 touch, an AXP2101 PMIC and an
AW9523B IO expander on an internal I2C bus, 8 MB **quad** PSRAM. Brought up software-
only (sim + QEMU + build); the real picture, touch, sleep and battery are verified at
flash time by Alan + Hermes — see `missions/cores3-port/HANDOFF.md`.

Hardware facts are from M5GFX 0.2.32 / M5Unified 0.2.25 (the vendor source) and the
mission brief; the picture/colour-order/orientation defaults are best-derived and
flagged for bench confirmation (below).

## The board, against the Waveshare AMOLED

| | Waveshare 1.8" AMOLED | M5Stack CoreS3 |
|---|---|---|
| Chip | ESP32-S3, 16 MB flash, 8 MB **octal** PSRAM | ESP32-S3, 16 MB flash, 8 MB **quad** PSRAM (`CONFIG_SPIRAM_MODE_QUAD`) |
| Screen | SH8601 / CO5300 AMOLED over QSPI, 368 x 448 | ILI9342C (ILI9341 family) IPS over 4-wire SPI, 320 x 240, inversion on |
| Screen power | panel rails off the PMIC | no power/reset GPIO: VCI off the **SY7088 boost** (AW9523B P1_7), reset off **AW9523B P1_1** |
| Backlight | PMIC | **AXP2101 DLDO1** (enable reg 0x90 bit7, voltage reg 0x99 = (level+641)>>5) — not a PWM pin |
| Touch | FT3168 / CST816 | **FT6336** (FT5x06 family, I2C 0x38), polled; INT is behind the expander (GPIO21), so not wired |
| Audio | ES8311 + NS4150B | **AW88298** amp (not ES8311) — audio stub |
| Power | AXP2101 PMIC, fuel gauge, PWR key | **AXP2101** PMIC (0x34), fuel gauge + PWR key, **+ AW9523B** IO expander (0x58) |
| IMU | QMI8658 (0x6B) | **BMI270** (no driver) or none on the SE: IMU **off**, firmware is no-IMU / SE-safe |
| Buttons | PWR key | no BOOT user key; the PWR key is the AXP2101 PWRON only |
| Clock | PCF85063 RTC | BM8563 RTC (0x51) present on the bus (not yet wired here) |

## Building

Its own build directory and sdkconfig, so the other boards' builds are untouched:
`sdkconfig.defaults` then `sdkconfig.defaults.cores3` on top. ESP-IDF **v5.4.1** (the
script defaults `IDF_PATH` to v5.5, which this host lacks — override it):

```sh
export IDF_PATH=~/esp/esp-idf          # v5.4.1
tools/build_cores3.sh                   # build only (no <port> = the rig never flashes)
```

which runs:

```sh
idf.py -B build_cores3 -D SDKCONFIG=build_cores3/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.cores3" build
```

Flashing is the human's step (Alan + Hermes; the board's factory firmware is backed
up). The flash packet — offsets, settings, and the on-device pass criteria — is in
`missions/cores3-port/HANDOFF.md`.

## I2C — the internal bus

SDA **GPIO12**, SCL **GPIO11**, 400 kHz. On it: the AXP2101 PMIC (0x34), the AW9523B
IO expander (0x58, reg 0x10 reads 0x23), the FT6336 touch (0x38), and on a full CoreS3
the GC0308 camera / BMI270 / BM8563 (unused here). The display port owns the bus
(`board_i2c_bus()`), as on the AMOLED.

## Pre-panel bring-up (slice 02)

The ILI9342C has no power or reset GPIO, so the PMIC and expander come up before any
panel traffic, mirroring the vendor sequence (`display_port_spi.c` `cores3_power_up`):

- **AW9523B** (0x58): output levels + direction + GPIO (not LED) mode; **P1_7** enables
  the SY7088 boost (the panel's VCI), **P1_1** driven high releases the LCD reset.
- **AXP2101** (0x34): DCDC UVP power-off latch off (0x23); LDOs on (0x90); ALDO1 1.8 V
  (AW88298 amp), ALDO2 3.3 V (ES7210 mic), ALDO3 3.3 V (camera), ALDO4 3.3 V (TF slot).

Best-effort: with no chip present (QEMU) each write logs and continues rather than hang.

## Display (slice 03)

ILI9342C rides the shared `display_port_spi.c` ILI9341-family driver
(`CONFIG_POCKET_TANK_DISPLAY_ILI9341`). Pins (`board_pins.h`): MOSI **37**, SCLK **36**,
CS **3**, D/C **35** (shares GPIO35 with the unused MISO — write-only, MISO = -1),
40 MHz, inversion **on**, 320 x 240 landscape (native portrait 240 x 320 scanned via
MADCTL). TF card CS (GPIO4) held high off the shared bus.

**Backlight** is AXP2101 DLDO1, driven from `backlight_set` via
`battery_port_dldo1_backlight` (enable 0x90 bit7, voltage 0x99 = (level+641)>>5, exactly
M5GFX `Light_M5StackCoreS3`). DLDO1 is dropped from the boot rail-trim for CoreS3 so the
backlight is not killed at boot.

## Touch (slice 04)

FT6336 at 0x38, polled every frame (`int_gpio=-1`: the INT routes FT6336 → AW9523B P1_2
→ INTN → GPIO21, behind the expander, so it is not wired for polling). Reset is the
AW9523B net (`PIN_TP_RST=-1`). The chip reports **landscape** raw 0..319 x 0..239 (M5GFX
`Touch_M5StackCoreS3`: x_max 319 / y_max 239, offset_rotation 0, no mirror), so CoreS3
uses an **identity** map (`TOUCH_SWAP_XY=0`, no mirror, no turn, x_max/y_max = TANK_W-1 /
TANK_H-1) — unlike the CYD's portrait-raw + swap. The right-side-up (180 / mirror) is a
best-guess, confirmed on glass (see HANDOFF).

## Sleep / wake + no-IMU (slice 05)

- **No IMU**: both IMU drivers are off (`IMU_QMI8658=n`, `IMU_MPU6050=n`), so `imu_port`
  finds no chip and every call no-ops — the firmware boots and runs with no IMU (SE-safe).
  The screen follows the SCREEN setting (no auto-flip).
- **Sleep/wake**: no BOOT user key — the PWR key (AXP2101 PWRON) triggers sleep (short)
  and power-off (long), polled over I2C. Per M5Unified the PWRON and its IRQ are **not**
  wired to the ESP32, so the PWR key cannot wake a GPIO deep sleep; touch (GPIO21, AW9523B
  INTN) is the only deep-sleep wake. So an **indefinite CoreS3 sleep powers off** via the
  AXP2101 (the PWR key cold-boots, the save reloads) rather than a deep sleep it could not
  wake from; a timer sleep still deep-sleeps and wakes on the timer. Touch-wake deep sleep
  (resume-in-place) is a future enhancement, not built (needs the FT6336/AW9523B INT path +
  bench validation).
- **Fuel gauge**: the shared `battery_port_axp2101.c` on the internal bus, reused as-is.

## Memory

8 MB **quad** PSRAM (not the octal of the other three boards): `CONFIG_SPIRAM_MODE_QUAD`.
Confirm quad at the bench via the boot log.

## Flash-time residue (Alan + Hermes)

Not software-verifiable; verified on the board (checklist in
`missions/cores3-port/HANDOFF.md`): the real picture + colour order + inversion, touch
tap-lands-where-touched (the swap/mirror/turn right-side-up), backlight brightness, sleep
= power-off + PWR-key cold-boot + sleep current, no-IMU boot, battery %, PSRAM quad, and
the fps for all six fish styles.
