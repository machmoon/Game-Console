# Battleship / LED Matrix Game Console

Arduino + MAX7219 8x32 project with multiple modes:
- Battleship (core game)
- Multi-game console (Battleship, Snake, Dino, Surf, React, Parkour)
- Screen mirror mode (PC -> LED matrix)
- Wii-style controller experiments

## Hardware
- MAX7219 4-in-1 (8x32), `DIN D11`, `CS D10`, `CLK D13`
- Joystick: `A0`, `A1`, `A2 (button)`
- Optional buttons/sensors depending on sketch

## Repo Layout
- `src/Battleship_MAX7219/Battleship_MAX7219.ino`
  - Original Battleship-focused sketch
- `src/Game_Console/Game_Console.ino`
  - Boot screen + menu + games:
    - Battleship
    - Snake
    - Dino Jump
    - Surf (basic Subway-style lanes)
    - React (joystick direction reaction)
    - Parkour (endless jump/platform)
- `src/Screen_Mirror_Receiver/Screen_Mirror_Receiver.ino`
  - Receives serial frames and displays them on MAX7219
- `src/Wii_Tank_Controller/Wii_Tank_Controller.ino`
  - Arduino controller sender for desktop Wii-tanks GUI
- `src/Wii_Tank_Level/Wii_Tank_Level.ino`
  - On-matrix Wii-tank style level
- `src/Water_MPU_Sim/Water_MPU_Sim.ino`
  - MPU6050 tilt-based water simulation

- `tools/wii_tanks_gui.py`
  - Fullscreen desktop game using Arduino controller input
- `tools/sound_bridge.py`
  - Serial event -> PC sound playback bridge
- `tools/screen_to_matrix.py`
  - Captures PC screen, downsamples to 32x8, streams to receiver sketch
- `tools/make_poster_diagrams.py`
  - Generates project architecture/wiring diagrams for poster

## Quick Start
### A) Game Console on LED Matrix
1. Upload `src/Game_Console/Game_Console.ino`
2. Use joystick to navigate menu and play

### B) Mirror Computer Screen to LED Matrix
1. Upload `src/Screen_Mirror_Receiver/Screen_Mirror_Receiver.ino`
2. Install Python deps:
   - `pip install pillow pyserial`
3. Run:
   - `python3 tools/screen_to_matrix.py --port /dev/cu.usbmodem1101 --fps 15`

### C) Wii Tanks on Laptop (Arduino as controller)
1. Upload `src/Wii_Tank_Controller/Wii_Tank_Controller.ino`
2. Install Python deps:
   - `pip install pygame pyserial`
3. Run:
   - `python3 tools/wii_tanks_gui.py --port /dev/cu.usbmodem1101 --force-controller`

## Notes
- Only one app can use the serial port at a time.
- Close Arduino Serial Monitor before running Python serial tools.
