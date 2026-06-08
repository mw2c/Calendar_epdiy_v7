# Calendar_epdiy_v7

ESP-IDF project for an epdiy V7-based e-paper calendar.

## Build

Activate your ESP-IDF environment from the repository root, then run:

```powershell
idf.py set-target esp32s3
idf.py build
```

## Flash

```powershell
idf.py flash monitor
```

## Hardware Notes

The board has three resistor-ladder ADC buttons sharing one input:

- Button ADC input: `GPIO19`
- ESP32-S3 ADC mapping: `ADC2 channel 8`
- Idle raw value: `4095`
- Button raw values: `970`, `1993`, and `2775`

Suggested initial thresholds:

```text
raw < 1400        button 3
1400..2399        button 2
2400..3399        button 1
raw >= 3600       no button
```

`GPIO19` and `GPIO20` are also the ESP32-S3 native USB D-/D+ pins on many boards. This project has been using the UART/CH340 port for flashing and monitor, so keep that in mind before enabling native USB console/JTAG features.

## Layout

- `main/`: application entry point and calendar code.
- `components/epdiy2/`: vendored epdiy driver component.

Keep application code out of `components/epdiy2/src` unless you are intentionally patching the driver.
