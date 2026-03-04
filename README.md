# Battleship on MAX7219 4-in-1 + Joystick (Arduino)

This is a full playable mini Battleship game for:
- `HiLetgo MAX7219 Dot Matrix Module 4-in-1 (5-pin)`
- 1 analog joystick module (X, Y, SW)

## 1) Arduino Libraries
Install this library from Arduino Library Manager:
- `MD_MAX72XX` by MajicDesigns

`SPI` is built in.

## 2) Wiring

### MAX7219 4-in-1 module
- `VCC` -> `5V`
- `GND` -> `GND`
- `DIN` -> `D11`
- `CS`  -> `D10`
- `CLK` -> `D13`

### Joystick module
- `VCC` -> `5V`
- `GND` -> `GND`
- `VRx` -> `A0`
- `VRy` -> `A1`
- `SW`  -> `A2`

## 3) Upload
Open:
- `src/Battleship_MAX7219.ino`

Select your board/port, then upload.

## 4) How to Play
- Press joystick button to start.
- Player 1 places 4 ships on the left board (move + press).
- Then Player 2 places 4 ships on the right board (move + press).
- Move joystick to move the target cursor on the middle shot board of the active player.
- Press joystick button to fire.
- Turns alternate: Player 1, then Player 2.
- Press joystick button on game-over screen to go back to start.

## 5) Display Layout (left to right)
- Module 1 (left): Player 1 board
- Module 2 (middle-left): Player 1 shot attempts
- Module 3 (middle-right): Player 2 shot attempts
- Module 4 (right): Player 2 board

Each board is 4x4 cells, and each cell is drawn as 2x2 pixels.

## 6) Display Legend
- Empty: off
- Ship (board): diagonal pixels
- Miss: 1 pixel
- Hit: full 2x2 block
- Cursor (active player shot board): opposite diagonal blinking

## 7) Current Game Rules
- Board size: `4x4`
- Ships per side: `4` single-cell ships
- Player vs Player turns (no AI)
- Ship placement is manual at game start

If you want, I can add next:
1. Manual ship placement for each player
2. Multi-cell ships with orientation
3. Turn handoff screen (hide the other side before passing)
4. Score + win/loss counter
# Battleship
