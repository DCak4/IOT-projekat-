# co-alarm-esp32

ESP32 alarm: an LED lights up when the temperature (DHT11) **and** CO (MQ-7) both cross a threshold. An OLED shows the readings and the status (`OK`, `Zagrevanje 60 s`, `CO detektovan`, `ALARM: CO + TEMP`, `Greska DHT11`).

## Wiring

| Part | Pin | ESP32 |
|---|---|---|
| DHT11 | S | GPIO4 (D4) |
| DHT11 | + / - | 3V3 / GND |
| MQ-7 | AO | 10 kΩ → **GPIO34 (D34)** → 15 kΩ → GND (voltage divider) |
| MQ-7 | VCC / GND | 5V / GND |
| OLED SSD1306 (I2C, 0x3C) | SDA / SCL | GPIO21 / GPIO22 |
| OLED | VCC / GND | 3V3 / GND |
| LED | anode → 220–330 Ω | GPIO25 (D25) |
| LED | cathode | GND |

> **Never connect MQ-7 AO straight to GPIO34.** The module runs on 5 V and the ESP32 pins are not 5 V tolerant. Check with a multimeter that GPIO34 never sees more than ~3 V.

## Prerequisites

- **ESP-IDF v6.1**
- **ESP32 development board** connected via USB

## Build, flash, monitor (Windows, PowerShell)

ESP-IDF does not handle spaces in paths well, so map the parent folder to a drive letter first (once per Windows session):

```powershell
D:\Espressif\frameworks\esp-idf-v6.1\export.ps1
subst X: "C:\Users\Majda Hamazagic\Desktop\RAF\Internet stvari"
cd X:\co-alarm-esp32
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the port shown in Device Manager. On Linux, activate with `. $HOME/esp/esp-idf/export.sh` and use e.g. `/dev/ttyACM0`.

If you get an OpenOCD server error, flash via UART: in the ESP-IDF extension, under Advanced, choose Flash with UART.

## Thresholds (menuconfig)

`idf.py menuconfig` → **Alarm Configuration**:

| Setting | Default |
|---|---|
| Temperature on / clear | 35 °C / 33 °C |
| CO on / clear (mV at GPIO34) | 900 / 750 (assumes ~500 mV in clean air — calibrate) |
| CO warm-up | 60 s |
| Test simulation | off |

Each condition must hold for 3 readings in a row to trigger. The DHT11 is read every 5 s and CO every 1 s.

### Test simulation (checking the LED)

Turn on **Alarm Configuration → Test simulation**, then build and flash. No sensors need to be connected. The firmware feeds fake temperature/CO values (derived from the thresholds) through 11 steps, drives the real LED and reads the GPIO25 level back after every sample. The monitor prints `PASS`/`FAIL` for each sample and step, and a summary per round; the round repeats every ~45 s.

The LED should light **only** during the steps `CO + temperatura` (from the 3rd sample), `Histerezis` and `Ponovo oba visoka` (from the 3rd sample). If the monitor says `PASS` but the LED stays dark, check the wiring (LED polarity, resistor, GPIO25).

Turn the option off again for normal operation.

### Calibrating CO

1. Run the sensor in fresh air for at least 10 minutes (a new MQ-7 is more stable after 24–48 h).
2. Read the `CO: ... mV` values in the monitor and take the average as the baseline.
3. Set **on = baseline + 400 mV** and **clear = baseline + 250 mV**.

The MQ-7 reading is only an indication, not a measured ppm value. This is a class project, not a certified CO detector.
