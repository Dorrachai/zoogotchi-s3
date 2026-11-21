# ZooGotchi S3 — 3D Enclosure & Hardware Assembly Notes

Target Printer: **Bambu Lab X1 Carbon**
Fasteners: **4x M2 x 6mm self-tapping screws**
Board: **Waveshare ESP32-S3-Touch-LCD-1.69**
Batteries: **3.7V 1000mAh LiPo** + **Rechargeable RTC coin cell (ML414 / MS621FE)**

> [!CAUTION]
> **Check 1000mAh LiPo `MX1.25` Polarity BEFORE Plugging In:**
> Generic 3.7V 1000mAh LiPo packs frequently have Red (`+`) and Black (`-`) wires
> swapped relative to Waveshare's `MX1.25` battery connector. Inspect the `+` and `-`
> markings printed on the Waveshare PCB silkscreen and verify your connector matches
> (re-pin with tweezers if reversed). Reversed polarity can permanently damage the
> onboard `ETA6098` charger IC.

> [!CAUTION]
> **Rechargeable RTC Coin Cell ONLY (`SH1.0` Connector):**
> The `PCF85063A` RTC backup header has an active trickle-charging circuit.
> Connect **only** a rechargeable lithium-manganese/vanadium coin cell
> (such as **ML414** or **MS621FE** with an `SH1.0` 2-pin lead). Never attach a
> non-rechargeable primary `CR`-series coin cell.

---

## 1. Enclosure Design & Bambu Lab X1 Carbon Print Settings

Because the Waveshare ESP32-S3-Touch-LCD-1.69 is controlled by **capacitive touch**,
**6-axis IMU motion**, and the **onboard `PWR` & `BOOT` buttons**, your enclosure does
**not** require any external perfboards, soldered switches, or extra wires.

| Slicer Setting | Recommended Value (Bambu Studio) | Notes |
| :--- | :--- | :--- |
| Filament | **PETG** or **PLA+** | PETG resists warm hand/pocket temps better |
| Layer Height | `0.16 mm` Optimal | Smooth rounded display bezel lip |
| Wall Loops | **3–4 walls** | Essential so M2 self-tapping screws bite without cracking |
| Infill | `20%` Gyroid | Rigid shell without unnecessary weight |
| Supports | Tree (auto), build-plate only | Keeps USB-C and button overhangs clean |

---

## 2. Critical Fitment Dimensions

1. **Display Window & Rounded Corners (`240 x 280`):**
   - Center the active LCD window with a `0.3–0.4 mm` XY tolerance around the glass edge
     and support the PCB back perimeter with corner standoffs (no pressure on flex ribbon!).
   - Leave the top face flush around the glass so edge swipes (`SWIPE_LEFT/RIGHT/UP/DOWN`)
     and petting strokes feel effortless.
2. **3.7V 1000mAh LiPo Safety Pocket:**
   - Measure your exact pack's **L x W x T** with calipers before printing (common 1000mAh
     pouches are ~`50 x 34 x 6 mm` or `40 x 30 x 8 mm`).
   - Add `+1.0 mm` width/length clearance and `0.5 mm` foam or **Kapton tape** between the
     LiPo pouch and the rear SMD components/solder joints on the PCB so nothing ever pinches
     or punctures the cell.
3. **Rechargeable RTC Coin Cell Bay:**
   - Route a dedicated shallow pocket beside the LiPo (or along the upper edge) for the tiny
     rechargeable RTC cell, with a clean wire relief channel to the `SH1.0` RTC header.
4. **4x M2 x 6mm Self-Tapping Screw Bosses:**
   - **Upper shell pilot hole ID:** `1.65 – 1.75 mm` (blind depth `4.8 mm` so a 6mm screw
     passes cleanly through a `1.6 mm` back-shell flange without poking through the front!).
   - **Boss Outer Diameter (OD):** `4.4 – 4.8 mm` with at least 3 perimeter wall loops.
   - **Lower shell through-hole ID:** `2.3 mm` with a `4.2 mm` countersink/counterbore (`1.5 mm` deep).
5. **External Cutouts & Acoustic Vents:**
   - **USB-C port cutout:** `10.5 x 4.8 mm` flared slightly for thick USB-C cables.
   - **`PWR` (GPIO40) & `BOOT` (GPIO0) buttons:** Use thin cantilevered U-shaped print flex-tabs
     (`0.8 mm` thick hinge) pressing the onboard tactile buttons cleanly.
   - **Passive Buzzer (`GPIO42`) acoustic slots:** 3–4 slim slots (`1.2 x 6 mm`) over the
     buzzer cavity so species chirps, melodies, and Rhythm Chirp cues sound crisp.

---

## 3. Verified Hardware Pinout Reference

| Peripheral | Interface | Pins |
| :--- | :--- | :--- |
| Display (`ST7789V2` 240x280) | SPI + PWM | `SCLK=6`, `MOSI=7`, `CS=5`, `DC=4`, `RST=8`, `BL=15` |
| Touch (`CST816T`) | I2C `0x15` | `SDA=11`, `SCL=10`, `RST=13`, `INT=14` |
| 6-Axis IMU (`QMI8658C`) | I2C `0x6B` | `SDA=11`, `SCL=10`, `INT1=38` |
| RTC (`PCF85063A`) | I2C `0x51` | `SDA=11`, `SCL=10`, `INT=39` |
| Passive Buzzer | LEDC ch0 | `GPIO42` (`50%` duty) |
| Battery ADC | ADC | `GPIO1` (`3.0x` divider) |
| Power Latch & Buttons | GPIO | `SYS_EN=41` (`OUTPUT HIGH`), `SYS_OUT=40` (`INPUT`), `BOOT=0` (`INPUT_PULLUP`) |
