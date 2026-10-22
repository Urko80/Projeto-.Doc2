# DOC AI S3CAM — XiaoZhi board

## First milestone included
- ESP32-S3 target.
- XiaoZhi's native Wi-Fi/network/protocol path (`WifiBoard` + shared `Application`).
- I2S simplex audio: MS3625 microphone and MAX98357A amplifier.
- BOOT button: enter Wi-Fi configuration while starting; otherwise toggle chat.

## Confirmed audio wiring
| Signal | GPIO |
|---|---:|
| Microphone BCLK/SCK | 1 |
| Microphone WS/LRCL | 2 |
| Microphone DOUT | 3 |
| MAX98357A BCLK | 41 |
| MAX98357A LRC | 42 |
| MAX98357A DIN | 47 |

## Display and camera status
The ST7735 1.77" and OV3660 pin maps are recorded in `config.h`, but this first
milestone intentionally does not initialize them. The XiaoZhi project has ST7789
support in common examples, but calling its ST7789 initializer is not a verified
ST7735 implementation. A real ST7735 driver/init sequence must be added before
enabling the display. Camera initialization is also a separate milestone.

PCA9685 SDA/SCL are not guessed. Add them after confirming the actual wiring.

## Install into the XiaoZhi repository
Run `install_board.ps1` from this package while PowerShell's current directory is
the root of the local `xiaozhi-esp32` repository. The script copies the board and
adds the board choice/build mapping without modifying existing board definitions.

Then build:
```powershell
python scripts/build.py doc-ai-s3cam --name doc-ai-s3cam
```

This package has not been compiled against the user's local checkout. A successful
build and physical audio/network test are still required.
