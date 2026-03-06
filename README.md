#LED Matrix Console

Arduino + MAX7219 (8x32) project with a game console and tools.

## Wiring (common)
- MAX7219: `DIN D11`, `CS D10`, `CLK D13`, `5V`, `GND`
- Joystick: `A0` (X), `A1` (Y), `A2` (button), `5V`, `GND`

## Main sketches
- `src/Game_Console/Game_Console.ino`
  - Boot/menu + games: Battleship, Snake, Dino, Surf, React, Parkour
- `src/Battleship_MAX7219/Battleship_MAX7219.ino`
  - Battleship-focused standalone version
- `src/Screen_Mirror_Receiver/Screen_Mirror_Receiver.ino`
  - Receives serial video frames for matrix mirroring

## Tools
- `tools/screen_to_matrix.py`
  - Mirrors PC screen to matrix receiver sketch
- `tools/wii_tanks_gui.py`
  - Desktop Wii-style tank game (Arduino as controller)
- `tools/sound_bridge.py`
  - Serial event -> PC sound playback

## Quick start
### 1) Play on matrix
Upload:
- `src/Game_Console/Game_Console.ino`

### 2) Mirror computer screen to matrix
1. Upload `src/Screen_Mirror_Receiver/Screen_Mirror_Receiver.ino`
2. Install deps: `pip install pillow pyserial`
3. Run: `python3 tools/screen_to_matrix.py --port /dev/cu.usbmodem1101 --fps 15`

### 3) Wii tanks on laptop
1. Upload `src/Wii_Tank_Controller/Wii_Tank_Controller.ino`
2. Install deps: `pip install pygame pyserial`
3. Run: `python3 tools/wii_tanks_gui.py --port /dev/cu.usbmodem1101 --force-controller`

## Note
Only one app can use the serial port at a time. Close Arduino Serial Monitor before running Python tools.
