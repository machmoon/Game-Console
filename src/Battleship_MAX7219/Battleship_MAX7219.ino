#include <SPI.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>

// Hardware pins and display setup
#define DEVICE_COUNT 4
#define DIN_PIN 11
#define CLK_PIN 13
#define CS_PIN 10
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

#define JOY_X_PIN A0
#define JOY_Y_PIN A1
#define JOY_SW_PIN A2
#define BUZZER_PIN 7

MD_Parola intro_display = MD_Parola(HARDWARE_TYPE, CS_PIN, DEVICE_COUNT);
const char intro_text[] = "WELCOME TO BATTLESHIP! PRESS TO START.";

// Basic game settings
const uint8_t BOARD_SIZE = 4;
const uint8_t SHIP_COUNT = 4;

const uint8_t ORIGIN_P1_BOARD = 0;   // left-most module
const uint8_t ORIGIN_P1_SHOTS = 8;   // middle-left module
const uint8_t ORIGIN_P2_SHOTS = 16;  // middle-right module
const uint8_t ORIGIN_P2_BOARD = 24;  // right-most module

enum CellState : uint8_t {
  CELL_EMPTY = 0,
  CELL_SHIP,
  CELL_MISS,
  CELL_HIT
};

enum ShotState : uint8_t {
  SHOT_NONE = 0,
  SHOT_MISS,
  SHOT_HIT
};

enum GameState : uint8_t {
  STATE_START = 0,
  STATE_PLACE_P1,
  STATE_PLACE_P2,
  STATE_P1_TURN,
  STATE_P2_TURN,
  STATE_GAME_OVER
};

CellState player1_board[BOARD_SIZE][BOARD_SIZE];
CellState player2_board[BOARD_SIZE][BOARD_SIZE];

ShotState player1_shots[BOARD_SIZE][BOARD_SIZE];
ShotState player2_shots[BOARD_SIZE][BOARD_SIZE];

GameState game_state = STATE_START;
uint8_t winner_player = 0;
bool win_song_played = false;
uint8_t player1_ships_placed = 0;
uint8_t player2_ships_placed = 0;

uint8_t cursor_x = 0;
uint8_t cursor_y = 0;

unsigned long last_move_at = 0;
unsigned long last_blink_at = 0;
bool blink_on = true;

const uint16_t MOVE_REPEAT_MS = 180;
const uint16_t BLINK_MS = 250;

// Display buffer: [row][device], each byte is one 8-pixel row of that module.
uint8_t matrix_rows[8][DEVICE_COUNT];

const bool FLIP_X = false;
const bool FLIP_Y = false;

// MAX7219 registers
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
  send_all(REG_INTENSITY, 3);
  send_all(REG_SHUTDOWN, 1);

  clear_matrix();


  update_matrix();
}

void play_tone_note(uint16_t freq_hz, uint16_t note_ms, uint16_t gap_ms) {
  tone(BUZZER_PIN, freq_hz, note_ms);
  delay(note_ms + gap_ms);
  noTone(BUZZER_PIN);
}

void play_hit_sound() {
  play_tone_note(1200, 70, 8);
  play_tone_note(1650, 80, 0);
}

void play_miss_sound() {
  play_tone_note(520, 80, 6);
  play_tone_note(360, 120, 0);
}

void play_win_song() {
  play_tone_note(784, 120, 25);   // G5
  play_tone_note(988, 120, 25);   // B5
  play_tone_note(1175, 120, 25);  // D6
  play_tone_note(1568, 220, 40);  // G6
  play_tone_note(1319, 140, 20);  // E6
  play_tone_note(1568, 280, 0);   // G6
}

void clear_cell_board(CellState board[][BOARD_SIZE]) {

  for (uint8_t y = 0; y < BOARD_SIZE; y++) {
    for (uint8_t x = 0; x < BOARD_SIZE; x++) {
      board[y][x] = CELL_EMPTY;
    }
  }
}

void clear_shot_board(ShotState board[][BOARD_SIZE]) {
  for (uint8_t y = 0; y < BOARD_SIZE; y++) {
    for (uint8_t x = 0; x < BOARD_SIZE; x++) {
      board[y][x] = SHOT_NONE;
    }
  }
}

uint8_t count_remaining_ships(CellState board[][BOARD_SIZE]) {
  uint8_t ships = 0;
  for (uint8_t y = 0; y < BOARD_SIZE; y++) {
    for (uint8_t x = 0; x < BOARD_SIZE; x++) {
      if (board[y][x] == CELL_SHIP) ships++;
    }
  }
  return ships;
}

void start_new_game() {

  clear_cell_board(player1_board);
  clear_cell_board(player2_board);
  clear_shot_board(player1_shots);
  clear_shot_board(player2_shots);
  player1_ships_placed = 0;
  player2_ships_placed = 0;
  cursor_x = 0;
  cursor_y = 0;
  winner_player = 0;
  win_song_played = false;
  game_state = STATE_PLACE_P1;
}

uint8_t cell_base_x(uint8_t origin_x, uint8_t cell_x) {
  return origin_x + cell_x * 2;
}

uint8_t cell_base_y(uint8_t cell_y) {
  return cell_y * 2;
}

void draw_cell_pattern(uint8_t base_x, uint8_t base_y, CellState state, bool cursor_here) {
  
  bool p00 = false, p10 = false, p01 = false, p11 = false;

  switch (state) {
    case CELL_EMPTY:
      break;
    case CELL_SHIP:
      p00 = true;
      p11 = true;
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
    p10 = true;
    p01 = true;
  }

  set_pixel(base_y, base_x, p00);
  set_pixel(base_y, base_x + 1, p10);
  set_pixel(base_y + 1, base_x, p01);
  set_pixel(base_y + 1, base_x + 1, p11);
}

void draw_shot_pattern(uint8_t base_x, uint8_t base_y, ShotState shot, bool cursor_here) {
  bool p00 = false, p10 = false, p01 = false, p11 = false;

  if (shot == SHOT_MISS) {
    p00 = true;
  } else if (shot == SHOT_HIT) {
    p00 = true;
    p10 = true;
    p01 = true;
    p11 = true;
  }

  if (cursor_here && blink_on) {
    p10 = true;
    p01 = true;
  }

  set_pixel(base_y, base_x, p00);
  set_pixel(base_y, base_x + 1, p10);
  set_pixel(base_y + 1, base_x, p01);
  set_pixel(base_y + 1, base_x + 1, p11);


}

void draw_turn_marker() {
  // if (game_state == STATE_PLACE_P1 && blink_on) {
  //   set_pixel(7, ORIGIN_P1_BOARD, true);
  //   set_pixel(7, ORIGIN_P1_BOARD + 1, true);
  // } else if (game_state == STATE_PLACE_P2 && blink_on) {
  //   set_pixel(7, ORIGIN_P2_BOARD + 6, true);
  //   set_pixel(7, ORIGIN_P2_BOARD + 7, true);
  // } else if (game_state == STATE_P1_TURN && blink_on) {
  //   set_pixel(7, ORIGIN_P1_SHOTS, true);
  //   set_pixel(7, ORIGIN_P1_SHOTS + 1, true);
  // } else if (game_state == STATE_P2_TURN && blink_on) {
  //   set_pixel(7, ORIGIN_P2_SHOTS + 6, true);
  //   set_pixel(7, ORIGIN_P2_SHOTS + 7, true);
  // } else 
  if (game_state == STATE_GAME_OVER && blink_on) {
    for (uint8_t y = 0; y < 8; y++) {
      for (uint8_t x = 0; x < 32; x++) {
        set_pixel(y, x, true);
      }
    }
  }
}

void render_game() {
  clear_matrix();

  for (uint8_t y = 0; y < BOARD_SIZE; y++) {

    for (uint8_t x = 0; x < BOARD_SIZE; x++) {
      uint8_t py = cell_base_y(y);

      uint8_t p1_board_x = cell_base_x(ORIGIN_P1_BOARD, x);
      uint8_t p1_shots_x = cell_base_x(ORIGIN_P1_SHOTS, x);
      uint8_t p2_shots_x = cell_base_x(ORIGIN_P2_SHOTS, x);
      uint8_t p2_board_x = cell_base_x(ORIGIN_P2_BOARD, x);

      draw_cell_pattern(p1_board_x, py, player1_board[y][x], false);
      draw_cell_pattern(p2_board_x, py, player2_board[y][x], false);

      bool p1_place_cursor = (game_state == STATE_PLACE_P1 && x == cursor_x && y == cursor_y);
      bool p2_place_cursor = (game_state == STATE_PLACE_P2 && x == cursor_x && y == cursor_y);
      bool p1_cursor = (game_state == STATE_P1_TURN && x == cursor_x && y == cursor_y);
      bool p2_cursor = (game_state == STATE_P2_TURN && x == cursor_x && y == cursor_y);

      if (p1_place_cursor) draw_cell_pattern(p1_board_x, py, player1_board[y][x], true);
      if (p2_place_cursor) draw_cell_pattern(p2_board_x, py, player2_board[y][x], true);

      draw_shot_pattern(p1_shots_x, py, player1_shots[y][x], p1_cursor);
      draw_shot_pattern(p2_shots_x, py, player2_shots[y][x], p2_cursor);
    }



  }

  draw_turn_marker();
  update_matrix();
}

void draw_shot_matrix_blast(uint8_t attacker_player, uint8_t phase) {
  // Full 8x8 splash animation on the current shooter's shot module.
  uint8_t start_x = (attacker_player == 1) ? ORIGIN_P1_SHOTS : ORIGIN_P2_SHOTS;
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = start_x; x < start_x + 8; x++) {
      bool on = false;
      if (phase == 0) on = ((x + y) % 2 == 0);
      else if (phase == 1) on = true;
      else on = ((x + y) % 2 == 1);
      set_pixel(y, x, on);
    }
  }
}

void animate_hit(uint8_t attacker_player) {
  render_game();
  draw_shot_matrix_blast(attacker_player, 0);
  update_matrix();
  delay(70);

  render_game();
  draw_shot_matrix_blast(attacker_player, 1);
  update_matrix();
  delay(90);

  render_game();
  draw_shot_matrix_blast(attacker_player, 2);
  update_matrix();
  delay(70);

  render_game();
  update_matrix();
}

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

void fire_shot_for_player(uint8_t attacker) {

  ShotState (*attacker_shots)[BOARD_SIZE] = (attacker == 1) ? player1_shots : player2_shots;
  
  CellState (*defender_board)[BOARD_SIZE] = (attacker == 1) ? player2_board : player1_board;

  ShotState &shot = attacker_shots[cursor_y][cursor_x];
  if (shot != SHOT_NONE) return;

  CellState &target = defender_board[cursor_y][cursor_x];
  if (target == CELL_SHIP) {
    target = CELL_HIT;
    shot = SHOT_HIT;
    play_hit_sound();
    animate_hit(attacker);
  } else {

    if (target == CELL_EMPTY) target = CELL_MISS;
    shot = SHOT_MISS;
    play_miss_sound();
  }

  if (count_remaining_ships(defender_board) == 0) {
    winner_player = attacker;
    game_state = STATE_GAME_OVER;
    return;
  }

  game_state = (attacker == 1) ? STATE_P2_TURN : STATE_P1_TURN;
}

void place_ship_for_player(uint8_t player_id) {
  CellState (*board)[BOARD_SIZE] = (player_id == 1) ? player1_board : player2_board;
  uint8_t *placed_count = (player_id == 1) ? &player1_ships_placed : &player2_ships_placed;

  if (*placed_count >= SHIP_COUNT) return;
  if (board[cursor_y][cursor_x] != CELL_EMPTY) {
    play_miss_sound();
    return;
  }

  board[cursor_y][cursor_x] = CELL_SHIP;
  (*placed_count)++;
  play_hit_sound();

  if (*placed_count < SHIP_COUNT) return;

  cursor_x = 0;
  cursor_y = 0;
  if (player_id == 1) game_state = STATE_PLACE_P2;
  else game_state = STATE_P1_TURN;
}

void update_blink() {
  unsigned long now = millis();
  if (now - last_blink_at >= BLINK_MS) {
    blink_on = !blink_on;
    last_blink_at = now;
  }
}

void show_start_screen() {
  static bool intro_ready = false;
  if (!intro_ready) {
    intro_display.displayText(intro_text, PA_CENTER, 70, 800, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    intro_ready = true;
  }

  if (intro_display.displayAnimate()) {
    intro_display.displayReset();
  }

  if (button_pressed()) {
    intro_ready = false;
    start_new_game();
  }
}

void setup() {
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  max7219_init();
  intro_display.begin();
  intro_display.setIntensity(3);
  randomSeed(analogRead(A0) + analogRead(A1) + micros());
}


void loop() {
  update_blink();

  if (game_state == STATE_START) {

    show_start_screen();

    return;
  }

  if (game_state == STATE_PLACE_P1) {
    update_cursor_from_joystick();
    if (button_pressed()) place_ship_for_player(1);
  } else if (game_state == STATE_PLACE_P2) {
    update_cursor_from_joystick();
    if (button_pressed()) place_ship_for_player(2);
  } else if (game_state == STATE_P1_TURN) {
    update_cursor_from_joystick();
    if (button_pressed()) fire_shot_for_player(1);
  } else if (game_state == STATE_P2_TURN) {
    update_cursor_from_joystick();
    if (button_pressed()) fire_shot_for_player(2);
  } else if (game_state == STATE_GAME_OVER) {
    if (!win_song_played) {
      play_win_song();
      win_song_played = true;
    }
    if (button_pressed()) game_state = STATE_START;
  }

  render_game();
}
