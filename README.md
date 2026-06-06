# Calendar_epdiy_v7

ESP-IDF project for an epdiy V7-based e-paper calendar.

## Build

Activate your ESP-IDF environment, then run:

```powershell
cd E:\Code\Calendar_epdiy_v7
idf.py set-target esp32s3
idf.py build
```

## Flash

```powershell
idf.py flash monitor
```

## Layout

- `main/`: application entry point and calendar code.
- `components/epdiy2/`: vendored epdiy driver component copied from `E:\Code\epdiy2`.

Keep application code out of `components/epdiy2/src` unless you are intentionally patching the driver.
