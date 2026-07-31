# Nano logger — pin map (single source of truth)

Board: Arduino Nano (ATmega328P, 5 V). Sensors read directly, NO divider.

| Function            | Nano pin(s)              | Bus     | Notes                          |
|---------------------|--------------------------|---------|--------------------------------|
| Hall sensors (6)    | A0, A1, A2, A3, A6, A7   | analog  | 5 V native; A4/A5 reserved     |
| DS3231 RTC          | A4 (SDA), A5 (SCL)       | I2C     | VCC=5V, GND=GND; no coin cell  |
| microSD module      | D10 CS, D11 MOSI, D12 MISO, D13 SCK | SPI | 5 V level-shifted module |
| (deploy) sensor gate| D3 -> IRLB8721 gate      | control | added in deployment layer      |
| (deploy) RTC SQW    | D2                       | interrupt | added in deployment layer    |

Note: D13 is SPI SCK *and* the onboard L LED — do not use the LED as a status
indicator; status goes to Serial.
