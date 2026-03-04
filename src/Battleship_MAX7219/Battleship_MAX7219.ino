#include <SPI.h>

// Hardware pins and display setup
#define DEVICE_COUNT 4
#define DIN_PIN 11
#define CLK_PIN 13
#define CS_PIN 10

#define JOY_X_PIN A0
#define JOY_Y_PIN A1
#define JOY_SW_PIN A2

// Basic game settings
const uint8_t BOARD_SIZE = 4;
const uint8_t SHIP_COUNT = 4; // single-cell ships keep rounds quick on this small display

enum CellState : uint8_t {
  CELL_EMPTY = 0,
  CELL_SHIP,
  CELL_MISS,
  CELL_HIT
};

enum GameState : uint8_t {
  STATE_START = 0,
  STATE_PLAYER_TURN,
  STATE_AI_TURN,
  STATE_GAME_OVER
};

CellState player_board[BOARD_SIZE][BOARD_SIZE];
CellState enemy_board[BOARD_SIZE][BOARD_SIZE];
GameState game_state = STATE_START;

uint8_t cursor_x = 0;
uint8_t cursor_y = 0;
bool player_won = false;

unsigned long last_move_at = 0;
unsigned long last_blink_at = 0;
bool blink_on = true;

const uint16_t MOVE_REPEAT_MS = 180;
const uint16_t BLINK_MS = 250;

// Display buffer: [row][device], each byte is one 8-pixel row of that module.
uint8_t matrix_rows[8][DEVICE_COUNT];

// Flip these if your module is mirrored/upside-down.
const bool FLIP_X = false;
const bool FLIP_Y = false;

// MAX7219 registers
const uint8_t REG_NOOP = 0x00;
const uint8_t REG_DIGIT0 = 0x01;
const uint8_t REG_DECODE_MODE = 0x09;
const uint8_t REG_INTENSITY = 0x0A;
const uint8_t REG_SCAN_LIMIT = 0x0B;
const uint8_t REG_SHUTDOWN = 0x0C;
const uint8_t REG_DISPLAY_TEST = 0x0F;

void send_all(uint8_t reg, uint8_t data) {
  digitalWrite(CS_PIN, LOW);
  for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
    SPI.transfer(reg);
    SPI.transfer(data);
  }
  digitalWrite(CS_PIN, HIGH);
}

void send_row(uint8_t reg, const uint8_t data_by_device[DEVICE_COUNT]) {
  digitalWrite(CS_PIN, LOW);

  // Shift farthest module first, nearest last.
  for (int8_t dev = DEVICE_COUNT - 1; dev >= 0; dev--) {
    SPI.transfer(reg);
    SPI.transfer(data_by_device[dev]);
  }

  digitalWrite(CS_PIN, HIGH);
}

void clear_matrix() {
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t dev = 0; dev < DEVICE_COUNT; dev++) {
      matrix_rows[y][dev] = 0;
    }
  }
}

void update_matrix() {
  for (uint8_t y = 0; y < 8; y++) {
    send_row(REG_DIGIT0 + y, matrix_rows[y]);
  }
}

void set_pixel(uint8_t y, uint8_t x, bool on) {
  if (x >= 32 || y >= 8) return;

  uint8_t draw_x = FLIP_X ? (31 - x) : x;
  uint8_t draw_y = FLIP_Y ? (7 - y) : y;

  uint8_t dev = draw_x / 8;
  uint8_t col = draw_x % 8;
  uint8_t bit_mask = (uint8_t)(1 << (7 - col));

  if (on) matrix_rows[draw_y][dev] |= bit_mask;
  else matrix_rows[draw_y][dev] &= (uint8_t)~bit_mask;
}

void max7219_init() {
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  SPI.begin();

  send_all(REG_SCAN_LIMIT, 7);
  send_all(REG_DECODE_MODE, 0);
  send_all(REG_DISPLAY_TEST, 0);
  send_all(REG_INTENSITY, 3); // 0..15
  send_all(REG_SHUTDOWN, 1);  // normal operation

  clear_matrix();
  update_matrix();
}

// Helpers
void clear_boards() {
  for (uint8_t y = 0; y < BOARD_SIZE; y++) {
    for (uint8_t x = 0; x < BOARD_SIZE; x++) {
      player_board[y][x] = CELL_EMPTY;
      enemy_board[y][x] = CELL_EMPTY;
    }
  }
}

void place_random_ships(CellState board[BOARD_SIZE][BOARD_SIZE]) {
  uint8_t placed = 0;
  while (placed < SHIP_COUNT) {
    uint8_t x = random(0, BOARD_SIZE);
    uint8_t y = random(0, BOARD_SIZE);
    if (board[y][x] == CELL_EMPTY) {
      board[y][x] = CELL_SHIP;
      placed++;
    }
  }
}

bool is_fireable(CellState cell) {
  return (cell == CELL_EMPTY || cell == CELL_SHIP);
}

uint8_t count_remaining_ships(CellState board[BOARD_SIZE][BOARD_SIZE]) {
  uint8_t ships = 0;
  for (uint8_t y = 0; y < BOARD_SIZE; y++) {
    for (uint8_t x = 0; x < BOARD_SIZE; x++) {
      if (board[y][x] == CELL_SHIP) ships++;
    }
  }
  return ships;
}

void start_new_game() {
  clear_boards();
  place_random_ships(player_board);
  place_random_ships(enemy_board);
  cursor_x = 0;
  cursor_y = 0;
  player_won = false;
  game_state = STATE_PLAYER_TURN;
}

// Screen layout:
// each board cell uses a 2x2 block of pixels.
// Player board is on the left (x 0..7), enemy board on the right (x 24..31).
uint8_t cell_base_x(bool is_enemy_board, uint8_t cell_x) {
  uint8_t board_origin_x = is_enemy_board ? 24 : 0;
  return board_origin_x + cell_x * 2;
}

uint8_t cell_base_y(uint8_t cell_y) {
  return cell_y * 2;
}

void draw_cell_pattern(uint8_t base_x, uint8_t base_y, CellState state, bool show_ship, bool cursor_here) {
  bool p00 = false, p10 = false, p01 = false, p11 = false;

  // 2x2 cell symbols:
  // miss = one dot, ship = diagonal, hit = full block
  switch (state) {
    case CELL_EMPTY:
      break;
    case CELL_SHIP:
      if (show_ship) {
        p00 = true;
        p11 = true;
      }
      break;
    case CELL_MISS:
      p00 = true;
      break;
    case CELL_HIT:
      p00 = true;
      p10 = true;
      p01 = true;
      p11 = true;
      break;
  }

  if (cursor_here && blink_on) {
    // Cursor shows up as the opposite diagonal.
    p10 = true;
    p01 = true;
  }

  set_pixel(base_y, base_x, p00);
  set_pixel(base_y, base_x + 1, p10);
  set_pixel(base_y + 1, base_x, p01);
  set_pixel(base_y + 1, base_x + 1, p11);
}

void draw_turn_marker() {
  // Middle area (x 13..18) shows turn state:
  // player turn = left marker, AI turn = right marker, game over = winner block
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 13; x <= 18; x++) {
      set_pixel(y, x, false);
    }
  }

  if (game_state == STATE_PLAYER_TURN) {
    set_pixel(3, 13, true); set_pixel(4, 13, true);
    set_pixel(3, 14, true); set_pixel(4, 14, true);
    set_pixel(3, 15, true); set_pixel(4, 15, true);
  } else if (game_state == STATE_AI_TURN) {
    set_pixel(3, 16, true); set_pixel(4, 16, true);
    set_pixel(3, 17, true); set_pixel(4, 17, true);
    set_pixel(3, 18, true); set_pixel(4, 18, true);
  } else if (game_state == STATE_GAME_OVER) {
    uint8_t x_start = player_won ? 13 : 16;
    for (uint8_t y = 2; y <= 5; y++) {
      for (uint8_t x = x_start; x < x_start + 3; x++) {
        set_pixel(y, x, blink_on);
      }
    }
  }
}

void render_boards() {
  clear_matrix();

  for (uint8_t y = 0; y < BOARD_SIZE; y++) {
    for (uint8_t x = 0; x < BOARD_SIZE; x++) {
      uint8_t left_x = cell_base_x(false, x);
      uint8_t right_x = cell_base_x(true, x);
      uint8_t px_y = cell_base_y(y);

      // Show your ships on your board.
      draw_cell_pattern(left_x, px_y, player_board[y][x], true, false);
      // Hide enemy ships until they get hit.
      bool cursor_here = (game_state == STATE_PLAYER_TURN && x == cursor_x && y == cursor_y);
      draw_cell_pattern(right_x, px_y, enemy_board[y][x], false, cursor_here);
    }
  }

  draw_turn_marker();
  update_matrix();
}

// Joystick input
bool button_pressed() {
  static bool last_state = HIGH;
  bool current = digitalRead(JOY_SW_PIN);
  bool pressed = (last_state == HIGH && current == LOW);
  last_state = current;
  return pressed;
}

int8_t axis_direction(int value) {
  if (value < 300) return -1;
  if (value > 700) return 1;
  return 0;
}

void update_cursor_from_joystick() {
  unsigned long now = millis();
  if (now - last_move_at < MOVE_REPEAT_MS) return;

  int8_t dx = axis_direction(analogRead(JOY_X_PIN));
  int8_t dy = axis_direction(analogRead(JOY_Y_PIN));
  if (dx == 0 && dy == 0) return;

  // Move one direction at a time; left/right takes priority.
  if (dx != 0) {
    int16_t next_x = (int16_t)cursor_x + dx;
    if (next_x >= 0 && next_x < BOARD_SIZE) {
      cursor_x = (uint8_t)next_x;
      last_move_at = now;
      return;
    }
  }

  if (dy != 0) {
    int16_t next_y = (int16_t)cursor_y + dy;
    if (next_y >= 0 && next_y < BOARD_SIZE) {
      cursor_y = (uint8_t)next_y;
      last_move_at = now;
      return;
    }
  }
}

// Game flow
void player_fire() {
  CellState &cell = enemy_board[cursor_y][cursor_x];
  if (!is_fireable(cell)) return;

  if (cell == CELL_SHIP) cell = CELL_HIT;
  else cell = CELL_MISS;

  if (count_remaining_ships(enemy_board) == 0) {
    player_won = true;
    game_state = STATE_GAME_OVER;
  } else {
    game_state = STATE_AI_TURN;
  }
}

void ai_fire() {
  // Simple AI: pick a random cell it hasn't fired at yet.
  uint8_t x, y;
  do {
    x = random(0, BOARD_SIZE);
    y = random(0, BOARD_SIZE);
  } while (!is_fireable(player_board[y][x]));

  if (player_board[y][x] == CELL_SHIP) player_board[y][x] = CELL_HIT;
  else player_board[y][x] = CELL_MISS;

  if (count_remaining_ships(player_board) == 0) {
    player_won = false;
    game_state = STATE_GAME_OVER;
  } else {
    game_state = STATE_PLAYER_TURN;
  }
}

void update_blink() {
  unsigned long now = millis();
  if (now - last_blink_at >= BLINK_MS) {
    blink_on = !blink_on;
    last_blink_at = now;
  }
}

void show_start_screen() {
  // Left icon = player side, right icon = enemy side.
  clear_matrix();

  for (uint8_t y = 1; y < 7; y++) {
    set_pixel(y, 1, true);
    set_pixel(y, 6, true);
    set_pixel(y, 25, true);
    set_pixel(y, 30, true);
  }
  for (uint8_t x = 1; x < 7; x++) {
    set_pixel(1, x, true);
    set_pixel(6, x, true);
    set_pixel(1, x + 24, true);
    set_pixel(6, x + 24, true);
  }

  // Blinking center box means "press to start."
  if (blink_on) {
    for (uint8_t y = 2; y <= 5; y++) {
      set_pixel(y, 14, true);
      set_pixel(y, 17, true);
    }
    for (uint8_t x = 14; x <= 17; x++) {
      set_pixel(2, x, true);
      set_pixel(5, x, true);
    }
  }

  update_matrix();
}

void setup() {
  pinMode(JOY_SW_PIN, INPUT_PULLUP);

  max7219_init();
  randomSeed(analogRead(A0) + analogRead(A1) + micros());
}

void loop() {
  update_blink();

  if (game_state == STATE_START) {
    show_start_screen();
    if (button_pressed()) start_new_game();
    return;
  }

  if (game_state == STATE_PLAYER_TURN) {
    update_cursor_from_joystick();
    if (button_pressed()) player_fire();
  } else if (game_state == STATE_AI_TURN) {
    // Tiny pause so you can see the AI turn happen.
    delay(250);
    ai_fire();
  } else if (game_state == STATE_GAME_OVER) {
    if (button_pressed()) game_state = STATE_START;
  }

  render_boards();
}
