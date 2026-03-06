#include <SPI.h>
#include <string.h>

#define DEVICE_COUNT 4
#define DIN_PIN 11
#define CLK_PIN 13
#define CS_PIN 10

#define JOY_X_PIN A0
#define JOY_Y_PIN A1
#define JOY_SW_PIN A2

const uint8_t REG_DIGIT0 = 0x01;
const uint8_t REG_DECODE_MODE = 0x09;
const uint8_t REG_INTENSITY = 0x0A;
const uint8_t REG_SCAN_LIMIT = 0x0B;
const uint8_t REG_SHUTDOWN = 0x0C;
const uint8_t REG_DISPLAY_TEST = 0x0F;

const uint16_t MOVE_REPEAT_MS = 150;
const uint16_t BLINK_MS = 220;

uint8_t matrix_rows[8][DEVICE_COUNT];

bool blink_on = true;
unsigned long last_blink_at = 0;
unsigned long last_move_at = 0;
bool last_button_down = false;

enum ConsoleState : uint8_t {
  STATE_BOOT = 0,
  STATE_MENU,
  STATE_BATTLESHIP,
  STATE_SNAKE,
  STATE_DINO,
  STATE_SURF,
  STATE_REACT,
  STATE_PARKOUR
};

ConsoleState console_state = STATE_BOOT;
unsigned long boot_started_at = 0;

uint8_t menu_index = 0;
const uint8_t MENU_COUNT = 6;

// ---------- battleship ----------
enum CellState : uint8_t {
  CELL_EMPTY = 0,
  CELL_SHIP,
  CELL_MISS,
  CELL_HIT
};

const uint8_t B_SIZE = 4;
const uint8_t B_SHIPS = 4;
CellState player_board[B_SIZE][B_SIZE];
CellState enemy_board[B_SIZE][B_SIZE];
CellState player_shots[B_SIZE][B_SIZE];
CellState enemy_shots[B_SIZE][B_SIZE];
uint8_t b_cursor_x = 0;
uint8_t b_cursor_y = 0;
bool b_game_over = false;
bool b_player_won = false;

// ---------- snake ----------
const uint8_t S_MAX = 80;
int8_t s_x[S_MAX];
int8_t s_y[S_MAX];
uint8_t s_len = 0;
int8_t s_dx = 1;
int8_t s_dy = 0;
int8_t s_food_x = 20;
int8_t s_food_y = 4;
unsigned long s_last_step_at = 0;
const uint16_t S_STEP_MS = 120;
bool s_game_over = false;
uint16_t s_score = 0;

// ---------- dino ----------
int8_t dino_y = 6;
int8_t dino_vy = 0;
bool dino_game_over = false;
uint16_t dino_score = 0;
bool dino_obs[32];
unsigned long dino_last_step_at = 0;
const uint16_t DINO_STEP_MS = 95;

// ---------- basic subway surfers ----------
struct SurfObstacle {
  int8_t x;
  int8_t lane;
  bool active;
};

const uint8_t SURF_MAX_OBS = 18;
SurfObstacle surf_obs[SURF_MAX_OBS];
int8_t surf_lane = 1; // 0,1,2
bool surf_game_over = false;
uint16_t surf_score = 0;
unsigned long surf_last_step_at = 0;
const uint16_t SURF_STEP_MS = 110;

// ---------- reaction ----------
int8_t react_target = 0; // 0 up, 1 right, 2 down, 3 left
unsigned long react_round_at = 0;
uint16_t react_window_ms = 1400;
uint16_t react_score = 0;
bool react_game_over = false;

// ---------- parkour ----------
uint8_t park_cols[32]; // 0 means gap, else first solid y (fill to 7)
int8_t park_player_y = 5;
int8_t park_player_vy = 0;
bool park_game_over = false;
uint16_t park_score = 0;
unsigned long park_last_step_at = 0;
const uint16_t PARK_STEP_MS = 100;
uint8_t park_gen_h = 6;

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
  for (int8_t d = DEVICE_COUNT - 1; d >= 0; d--) {
    SPI.transfer(reg);
    SPI.transfer(data_by_device[d]);
  }
  digitalWrite(CS_PIN, HIGH);
}

void clear_matrix() {
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t d = 0; d < DEVICE_COUNT; d++) {
      matrix_rows[y][d] = 0;
    }
  }
}

void update_matrix() {
  for (uint8_t y = 0; y < 8; y++) send_row(REG_DIGIT0 + y, matrix_rows[y]);
}

void set_pixel(int8_t y, int8_t x, bool on) {
  if (x < 0 || x >= 32 || y < 0 || y >= 8) return;
  uint8_t dev = (uint8_t)x / 8;
  uint8_t col = (uint8_t)x % 8;
  uint8_t mask = (uint8_t)(1 << (7 - col));
  if (on) matrix_rows[y][dev] |= mask;
  else matrix_rows[y][dev] &= (uint8_t)~mask;
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

void update_blink() {
  unsigned long now = millis();
  if (now - last_blink_at >= BLINK_MS) {
    blink_on = !blink_on;
    last_blink_at = now;
  }
}

bool button_edge() {
  bool down = (digitalRead(JOY_SW_PIN) == LOW);
  bool edge = (down && !last_button_down);
  last_button_down = down;
  return edge;
}

int8_t joy_dir_x() {
  int v = analogRead(JOY_X_PIN);
  if (v < 320) return -1;
  if (v > 700) return 1;
  return 0;
}

int8_t joy_dir_y() {
  int v = analogRead(JOY_Y_PIN);
  if (v < 320) return -1;
  if (v > 700) return 1;
  return 0;
}

int8_t joy_cardinal_dir() {
  int8_t dx = joy_dir_x();
  int8_t dy = joy_dir_y();
  if (dx == 0 && dy == 0) return -1;

  int vx = analogRead(JOY_X_PIN) - 512;
  int vy = analogRead(JOY_Y_PIN) - 512;
  if (abs(vx) > abs(vy)) return (vx > 0) ? 1 : 3;
  return (vy > 0) ? 2 : 0;
}

bool can_move_now() {
  unsigned long now = millis();
  if (now - last_move_at < MOVE_REPEAT_MS) return false;
  last_move_at = now;
  return true;
}

uint8_t char_col(char ch, uint8_t col) {
  switch (ch) {
    case 'A': { static const uint8_t c[5] = {0x7E,0x09,0x09,0x7E,0x00}; return c[col]; }
    case 'B': { static const uint8_t c[5] = {0x7F,0x49,0x49,0x36,0x00}; return c[col]; }
    case 'C': { static const uint8_t c[5] = {0x3E,0x41,0x41,0x22,0x00}; return c[col]; }
    case 'D': { static const uint8_t c[5] = {0x7F,0x41,0x41,0x3E,0x00}; return c[col]; }
    case 'E': { static const uint8_t c[5] = {0x7F,0x49,0x49,0x41,0x00}; return c[col]; }
    case 'F': { static const uint8_t c[5] = {0x7F,0x09,0x09,0x01,0x00}; return c[col]; }
    case 'G': { static const uint8_t c[5] = {0x3E,0x41,0x51,0x72,0x00}; return c[col]; }
    case 'H': { static const uint8_t c[5] = {0x7F,0x08,0x08,0x7F,0x00}; return c[col]; }
    case 'I': { static const uint8_t c[5] = {0x41,0x7F,0x41,0x00,0x00}; return c[col]; }
    case 'K': { static const uint8_t c[5] = {0x7F,0x08,0x14,0x63,0x00}; return c[col]; }
    case 'L': { static const uint8_t c[5] = {0x7F,0x40,0x40,0x40,0x00}; return c[col]; }
    case 'M': { static const uint8_t c[5] = {0x7F,0x02,0x04,0x02,0x7F}; return c[col]; }
    case 'N': { static const uint8_t c[5] = {0x7F,0x04,0x08,0x7F,0x00}; return c[col]; }
    case 'O': { static const uint8_t c[5] = {0x3E,0x41,0x41,0x3E,0x00}; return c[col]; }
    case 'P': { static const uint8_t c[5] = {0x7F,0x09,0x09,0x06,0x00}; return c[col]; }
    case 'R': { static const uint8_t c[5] = {0x7F,0x09,0x19,0x66,0x00}; return c[col]; }
    case 'S': { static const uint8_t c[5] = {0x26,0x49,0x49,0x32,0x00}; return c[col]; }
    case 'T': { static const uint8_t c[5] = {0x01,0x7F,0x01,0x01,0x00}; return c[col]; }
    case 'U': { static const uint8_t c[5] = {0x3F,0x40,0x40,0x3F,0x00}; return c[col]; }
    case 'V': { static const uint8_t c[5] = {0x1F,0x20,0x40,0x20,0x1F}; return c[col]; }
    case 'W': { static const uint8_t c[5] = {0x7F,0x20,0x10,0x20,0x7F}; return c[col]; }
    case 'Y': { static const uint8_t c[5] = {0x07,0x08,0x70,0x08,0x07}; return c[col]; }
    case ' ': return 0x00;
    default: return 0x00;
  }
}

void draw_text_center(const char* text) {
  uint8_t len = (uint8_t)strlen(text);
  uint8_t total_w = len * 6;
  int8_t start_x = (32 - total_w) / 2;
  if (start_x < 0) start_x = 0;

  for (uint8_t i = 0; i < len; i++) {
    for (uint8_t c = 0; c < 5; c++) {
      uint8_t bits = char_col(text[i], c);
      int8_t x = start_x + i * 6 + c;
      if (x < 0 || x >= 32) continue;
      for (uint8_t y = 0; y < 7; y++) {
        if (bits & (1 << y)) set_pixel(y, x, true);
      }
    }
  }
}

void draw_boot() {
  unsigned long t = millis() - boot_started_at;
  clear_matrix();

  for (uint8_t x = 0; x < 32; x++) {
    uint8_t y = (uint8_t)(3 + ((x + (t / 80)) % 3));
    set_pixel(y, x, true);
  }
  if (t > 900) {
    for (uint8_t x = 8; x < 24; x++) set_pixel(0, x, true);
  }

  update_matrix();
  if (t > 2200) console_state = STATE_MENU;
}

void draw_menu() {
  clear_matrix();

  if (menu_index > 0 && blink_on) {
    set_pixel(3, 0, true); set_pixel(2, 1, true); set_pixel(3, 1, true); set_pixel(4, 1, true);
  }
  if (menu_index + 1 < MENU_COUNT && blink_on) {
    set_pixel(3, 31, true); set_pixel(2, 30, true); set_pixel(3, 30, true); set_pixel(4, 30, true);
  }

  if (menu_index == 0) draw_text_center("BATTLE");
  else if (menu_index == 1) draw_text_center("SNAKE");
  else if (menu_index == 2) draw_text_center("DINO");
  else if (menu_index == 3) draw_text_center("SURF");
  else if (menu_index == 4) draw_text_center("REACT");
  else draw_text_center("PARK");

  for (uint8_t i = 0; i < MENU_COUNT; i++) {
    uint8_t x = 5 + i * 4;
    bool on = (i == menu_index) ? blink_on : true;
    set_pixel(7, x, on);
  }

  update_matrix();
}

// -------------------- battleship --------------------
void clear_cell_board(CellState b[][B_SIZE]) {
  for (uint8_t y = 0; y < B_SIZE; y++) {
    for (uint8_t x = 0; x < B_SIZE; x++) b[y][x] = CELL_EMPTY;
  }
}

void seed_random_ships(CellState b[][B_SIZE]) {
  uint8_t placed = 0;
  while (placed < B_SHIPS) {
    uint8_t x = random(0, B_SIZE);
    uint8_t y = random(0, B_SIZE);
    if (b[y][x] == CELL_EMPTY) {
      b[y][x] = CELL_SHIP;
      placed++;
    }
  }
}

void start_battleship() {
  clear_cell_board(player_board);
  clear_cell_board(enemy_board);
  clear_cell_board(player_shots);
  clear_cell_board(enemy_shots);
  seed_random_ships(player_board);
  seed_random_ships(enemy_board);
  b_cursor_x = 0;
  b_cursor_y = 0;
  b_game_over = false;
  b_player_won = false;
}

uint8_t remaining_ships(CellState b[][B_SIZE]) {
  uint8_t n = 0;
  for (uint8_t y = 0; y < B_SIZE; y++) {
    for (uint8_t x = 0; x < B_SIZE; x++) {
      if (b[y][x] == CELL_SHIP) n++;
    }
  }
  return n;
}

void draw_bs_cell(uint8_t ox, uint8_t oy, CellState s) {
  if (s == CELL_SHIP) {
    set_pixel(oy, ox, true);
    set_pixel(oy + 1, ox + 1, true);
  } else if (s == CELL_MISS) {
    set_pixel(oy, ox, true);
  } else if (s == CELL_HIT) {
    set_pixel(oy, ox, true);
    set_pixel(oy + 1, ox, true);
    set_pixel(oy, ox + 1, true);
    set_pixel(oy + 1, ox + 1, true);
  }
}

void ai_take_shot() {
  for (uint8_t tries = 0; tries < 40; tries++) {
    uint8_t x = random(0, B_SIZE);
    uint8_t y = random(0, B_SIZE);
    if (enemy_shots[y][x] == CELL_EMPTY) {
      if (player_board[y][x] == CELL_SHIP) {
        player_board[y][x] = CELL_HIT;
        enemy_shots[y][x] = CELL_HIT;
      } else if (player_board[y][x] == CELL_EMPTY) {
        player_board[y][x] = CELL_MISS;
        enemy_shots[y][x] = CELL_MISS;
      }
      return;
    }
  }
}

void draw_battleship() {
  clear_matrix();
  for (uint8_t y = 0; y < B_SIZE; y++) {
    for (uint8_t x = 0; x < B_SIZE; x++) {
      draw_bs_cell(x * 2, y * 2, player_board[y][x]);
      draw_bs_cell(8 + x * 2, y * 2, player_shots[y][x]);
    }
  }

  if (blink_on && !b_game_over) {
    set_pixel(b_cursor_y * 2, 8 + b_cursor_x * 2 + 1, true);
    set_pixel(b_cursor_y * 2 + 1, 8 + b_cursor_x * 2, true);
  }

  uint8_t e = remaining_ships(enemy_board);
  uint8_t p = remaining_ships(player_board);
  for (uint8_t i = 0; i < e; i++) set_pixel(0, 24 + i, true);
  for (uint8_t i = 0; i < p; i++) set_pixel(7, 24 + i, true);

  if (b_game_over && blink_on) {
    for (uint8_t x = 24; x < 32; x++) set_pixel(b_player_won ? 3 : 4, x, true);
  }

  update_matrix();
}

void update_battleship() {
  if (!b_game_over) {
    if (can_move_now()) {
      int8_t dx = joy_dir_x();
      int8_t dy = joy_dir_y();
      if (dx != 0) b_cursor_x = (uint8_t)constrain((int)b_cursor_x + dx, 0, (int)B_SIZE - 1);
      if (dy != 0) b_cursor_y = (uint8_t)constrain((int)b_cursor_y + dy, 0, (int)B_SIZE - 1);
    }

    if (button_edge() && player_shots[b_cursor_y][b_cursor_x] == CELL_EMPTY) {
      if (enemy_board[b_cursor_y][b_cursor_x] == CELL_SHIP) {
        enemy_board[b_cursor_y][b_cursor_x] = CELL_HIT;
        player_shots[b_cursor_y][b_cursor_x] = CELL_HIT;
      } else {
        player_shots[b_cursor_y][b_cursor_x] = CELL_MISS;
      }

      if (remaining_ships(enemy_board) == 0) {
        b_game_over = true;
        b_player_won = true;
      } else {
        ai_take_shot();
        if (remaining_ships(player_board) == 0) {
          b_game_over = true;
          b_player_won = false;
        }
      }
    }
  } else if (button_edge()) {
    console_state = STATE_MENU;
    return;
  }

  draw_battleship();
}

// -------------------- snake --------------------
bool snake_has(int8_t x, int8_t y) {
  for (uint8_t i = 0; i < s_len; i++) {
    if (s_x[i] == x && s_y[i] == y) return true;
  }
  return false;
}

void snake_spawn_food() {
  for (uint8_t t = 0; t < 90; t++) {
    int8_t x = random(0, 32);
    int8_t y = random(0, 8);
    if (!snake_has(x, y)) {
      s_food_x = x;
      s_food_y = y;
      return;
    }
  }
}

void start_snake() {
  s_len = 5;
  for (uint8_t i = 0; i < s_len; i++) {
    s_x[i] = 8 - i;
    s_y[i] = 3;
  }
  s_dx = 1;
  s_dy = 0;
  s_last_step_at = millis();
  s_game_over = false;
  s_score = 0;
  snake_spawn_food();
}

void draw_snake() {
  clear_matrix();
  for (uint8_t i = 0; i < s_len; i++) set_pixel(s_y[i], s_x[i], true);
  if (blink_on) set_pixel(s_food_y, s_food_x, true);
  if (s_game_over && blink_on) for (uint8_t x = 8; x < 24; x++) set_pixel(0, x, true);
  update_matrix();
}

void update_snake() {
  if (!s_game_over) {
    if (can_move_now()) {
      int8_t dx = joy_dir_x();
      int8_t dy = joy_dir_y();
      if (dx != 0 && s_dx == 0) { s_dx = dx; s_dy = 0; }
      if (dy != 0 && s_dy == 0) { s_dy = dy; s_dx = 0; }
    }

    unsigned long now = millis();
    if (now - s_last_step_at >= S_STEP_MS) {
      s_last_step_at = now;
      int8_t nx = s_x[0] + s_dx;
      int8_t ny = s_y[0] + s_dy;

      if (nx < 0 || nx >= 32 || ny < 0 || ny >= 8 || snake_has(nx, ny)) {
        s_game_over = true;
      } else {
        for (int8_t i = (int8_t)s_len; i > 0; i--) {
          s_x[i] = s_x[i - 1];
          s_y[i] = s_y[i - 1];
        }
        s_x[0] = nx;
        s_y[0] = ny;

        if (nx == s_food_x && ny == s_food_y) {
          if (s_len < S_MAX - 1) s_len++;
          s_score++;
          snake_spawn_food();
        }
      }
    }
  } else if (button_edge()) {
    console_state = STATE_MENU;
    return;
  }

  draw_snake();
}

// -------------------- dino jump --------------------
void start_dino() {
  dino_y = 6;
  dino_vy = 0;
  dino_game_over = false;
  dino_score = 0;
  for (uint8_t x = 0; x < 32; x++) dino_obs[x] = false;
  dino_last_step_at = millis();
}

void draw_dino() {
  clear_matrix();

  for (uint8_t x = 0; x < 32; x++) set_pixel(7, x, true); // ground
  set_pixel(dino_y, 4, true);
  set_pixel(dino_y - 1, 4, true);

  for (uint8_t x = 0; x < 32; x++) {
    if (dino_obs[x]) {
      set_pixel(6, x, true);
      set_pixel(5, x, true);
    }
  }

  uint8_t bars = (uint8_t)min((int)(dino_score / 10), 8);
  for (uint8_t i = 0; i < bars; i++) set_pixel(0, i, true);

  if (dino_game_over && blink_on) for (uint8_t x = 10; x < 22; x++) set_pixel(0, x, true);
  update_matrix();
}

void update_dino() {
  if (!dino_game_over) {
    if (button_edge() && dino_y >= 6) {
      dino_vy = -3;
    }

    unsigned long now = millis();
    if (now - dino_last_step_at >= DINO_STEP_MS) {
      dino_last_step_at = now;

      for (uint8_t x = 0; x < 31; x++) dino_obs[x] = dino_obs[x + 1];
      dino_obs[31] = (random(0, 100) < 22);

      dino_y += dino_vy;
      dino_vy += 1;
      if (dino_y > 6) { dino_y = 6; dino_vy = 0; }
      if (dino_y < 2) { dino_y = 2; }

      bool hit = dino_obs[4] && (dino_y >= 5);
      if (hit) dino_game_over = true;
      else dino_score++;
    }
  } else if (button_edge()) {
    console_state = STATE_MENU;
    return;
  }

  draw_dino();
}

// -------------------- subway basic --------------------
void start_surf() {
  surf_lane = 1;
  surf_game_over = false;
  surf_score = 0;
  for (uint8_t i = 0; i < SURF_MAX_OBS; i++) surf_obs[i].active = false;
  surf_last_step_at = millis();
}

void surf_spawn() {
  for (uint8_t i = 0; i < SURF_MAX_OBS; i++) {
    if (!surf_obs[i].active) {
      surf_obs[i].active = true;
      surf_obs[i].x = 31;
      surf_obs[i].lane = random(0, 3);
      return;
    }
  }
}

void draw_surf() {
  clear_matrix();

  // lane guides
  for (uint8_t x = 0; x < 32; x += 2) {
    set_pixel(2, x, true);
    set_pixel(4, x, true);
  }

  int8_t py = 1 + surf_lane * 2;
  set_pixel(py, 4, true);
  set_pixel(py, 5, true);

  for (uint8_t i = 0; i < SURF_MAX_OBS; i++) {
    if (!surf_obs[i].active) continue;
    int8_t y = 1 + surf_obs[i].lane * 2;
    set_pixel(y, surf_obs[i].x, true);
  }

  uint8_t bars = (uint8_t)min((int)(surf_score / 12), 8);
  for (uint8_t i = 0; i < bars; i++) set_pixel(0, i, true);

  if (surf_game_over && blink_on) for (uint8_t x = 10; x < 22; x++) set_pixel(7, x, true);
  update_matrix();
}

void update_surf() {
  if (!surf_game_over) {
    if (can_move_now()) {
      int8_t dy = joy_dir_y();
      if (dy < 0) surf_lane = max(0, surf_lane - 1);
      if (dy > 0) surf_lane = min(2, surf_lane + 1);
    }

    unsigned long now = millis();
    if (now - surf_last_step_at >= SURF_STEP_MS) {
      surf_last_step_at = now;

      for (uint8_t i = 0; i < SURF_MAX_OBS; i++) {
        if (!surf_obs[i].active) continue;
        surf_obs[i].x--;
        if (surf_obs[i].x < 0) surf_obs[i].active = false;
      }

      if (random(0, 100) < 35) surf_spawn();

      for (uint8_t i = 0; i < SURF_MAX_OBS; i++) {
        if (!surf_obs[i].active) continue;
        if (surf_obs[i].x == 4 && surf_obs[i].lane == surf_lane) {
          surf_game_over = true;
        }
      }

      if (!surf_game_over) surf_score++;
    }
  } else if (button_edge()) {
    console_state = STATE_MENU;
    return;
  }

  draw_surf();
}

// -------------------- reaction --------------------
void react_next_round() {
  react_target = random(0, 4);
  react_round_at = millis();
  if (react_window_ms > 550) react_window_ms -= 25;
}

void start_react() {
  react_score = 0;
  react_window_ms = 1400;
  react_game_over = false;
  react_next_round();
}

void draw_react_arrow(int8_t dir) {
  // dir: 0 up, 1 right, 2 down, 3 left
  int8_t cx = 16;
  int8_t cy = 3;
  if (dir == 0) {
    set_pixel(cy - 2, cx, true);
    set_pixel(cy - 1, cx - 1, true); set_pixel(cy - 1, cx, true); set_pixel(cy - 1, cx + 1, true);
    for (int8_t y = cy; y <= cy + 2; y++) set_pixel(y, cx, true);
  } else if (dir == 2) {
    set_pixel(cy + 2, cx, true);
    set_pixel(cy + 1, cx - 1, true); set_pixel(cy + 1, cx, true); set_pixel(cy + 1, cx + 1, true);
    for (int8_t y = cy - 2; y <= cy; y++) set_pixel(y, cx, true);
  } else if (dir == 1) {
    set_pixel(cy, cx + 2, true);
    set_pixel(cy - 1, cx + 1, true); set_pixel(cy, cx + 1, true); set_pixel(cy + 1, cx + 1, true);
    for (int8_t x = cx - 2; x <= cx; x++) set_pixel(cy, x, true);
  } else {
    set_pixel(cy, cx - 2, true);
    set_pixel(cy - 1, cx - 1, true); set_pixel(cy, cx - 1, true); set_pixel(cy + 1, cx - 1, true);
    for (int8_t x = cx; x <= cx + 2; x++) set_pixel(cy, x, true);
  }
}

void draw_react() {
  clear_matrix();
  draw_react_arrow(react_target);

  uint8_t bars = (uint8_t)min((int)react_score, 8);
  for (uint8_t i = 0; i < bars; i++) set_pixel(0, i, true);

  uint16_t elapsed = (uint16_t)(millis() - react_round_at);
  uint8_t time_bar = (elapsed >= react_window_ms) ? 0 : (uint8_t)(8 - (elapsed * 8 / react_window_ms));
  for (uint8_t i = 0; i < time_bar; i++) set_pixel(7, 31 - i, true);

  if (react_game_over && blink_on) for (uint8_t x = 10; x < 22; x++) set_pixel(0, x, true);
  update_matrix();
}

void update_react() {
  if (!react_game_over) {
    int8_t got = joy_cardinal_dir();
    if (got >= 0) {
      if (got == react_target) {
        react_score++;
        react_next_round();
      } else {
        react_game_over = true;
      }
    }

    if ((millis() - react_round_at) > react_window_ms) {
      react_game_over = true;
    }
  } else if (button_edge()) {
    console_state = STATE_MENU;
    return;
  }

  draw_react();
}

// -------------------- parkour --------------------
void start_parkour() {
  park_player_y = 5;
  park_player_vy = 0;
  park_game_over = false;
  park_score = 0;
  park_last_step_at = millis();
  park_gen_h = 6;

  for (uint8_t x = 0; x < 32; x++) {
    park_cols[x] = 6;
  }
}

void park_shift_left() {
  for (uint8_t x = 0; x < 31; x++) {
    park_cols[x] = park_cols[x + 1];
  }
}

void park_spawn_col() {
  if (random(0, 100) < 18) {
    park_cols[31] = 0;
    return;
  }

  int8_t delta = (int8_t)random(-1, 2);
  int8_t nh = (int8_t)park_gen_h + delta;
  if (nh < 4) nh = 4;
  if (nh > 7) nh = 7;
  park_gen_h = (uint8_t)nh;
  park_cols[31] = park_gen_h;
}

void draw_parkour() {
  clear_matrix();

  for (uint8_t x = 0; x < 32; x++) {
    uint8_t h = park_cols[x];
    if (h == 0) continue;
    for (uint8_t y = h; y < 8; y++) {
      set_pixel(y, x, true);
    }
  }

  set_pixel(park_player_y, 4, true);
  if (park_player_y > 0) set_pixel(park_player_y - 1, 4, true);

  uint8_t bars = (uint8_t)min((int)(park_score / 12), 8);
  for (uint8_t i = 0; i < bars; i++) set_pixel(0, i, true);

  if (park_game_over && blink_on) {
    for (uint8_t x = 10; x < 22; x++) set_pixel(0, x, true);
  }

  update_matrix();
}

void update_parkour() {
  if (!park_game_over) {
    uint8_t ground_h = park_cols[4];
    int8_t stand_y = (ground_h == 0) ? 7 : (int8_t)ground_h - 1;
    bool on_ground = (ground_h != 0 && park_player_y >= stand_y && park_player_vy >= 0);

    if ((button_edge() || joy_dir_y() < 0) && on_ground) {
      park_player_vy = -3;
    }

    unsigned long now = millis();
    if (now - park_last_step_at >= PARK_STEP_MS) {
      park_last_step_at = now;

      park_shift_left();
      park_spawn_col();

      park_player_y += park_player_vy;
      park_player_vy += 1;
      if (park_player_vy > 3) park_player_vy = 3;
      if (park_player_y < 0) park_player_y = 0;

      ground_h = park_cols[4];
      stand_y = (ground_h == 0) ? 7 : (int8_t)ground_h - 1;
      if (ground_h != 0 && park_player_y >= stand_y) {
        park_player_y = stand_y;
        park_player_vy = 0;
      }

      if (ground_h == 0 && park_player_y >= 7) {
        park_game_over = true;
      }

      if (!park_game_over) park_score++;
    }
  } else if (button_edge()) {
    console_state = STATE_MENU;
    return;
  }

  draw_parkour();
}

void setup() {
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  max7219_init();
  randomSeed(analogRead(A3));
  boot_started_at = millis();
}

void loop() {
  update_blink();

  if (console_state == STATE_BOOT) {
    draw_boot();
    return;
  }

  if (console_state == STATE_MENU) {
    if (can_move_now()) {
      int8_t dx = joy_dir_x();
      if (dx != 0) {
        int8_t next = (int8_t)menu_index + dx;
        if (next >= 0 && next < MENU_COUNT) menu_index = (uint8_t)next;
      }
    }

    if (button_edge()) {
      if (menu_index == 0) { start_battleship(); console_state = STATE_BATTLESHIP; }
      else if (menu_index == 1) { start_snake(); console_state = STATE_SNAKE; }
      else if (menu_index == 2) { start_dino(); console_state = STATE_DINO; }
      else if (menu_index == 3) { start_surf(); console_state = STATE_SURF; }
      else if (menu_index == 4) { start_react(); console_state = STATE_REACT; }
      else { start_parkour(); console_state = STATE_PARKOUR; }
    }

    draw_menu();
    return;
  }

  if (console_state == STATE_BATTLESHIP) { update_battleship(); return; }
  if (console_state == STATE_SNAKE) { update_snake(); return; }
  if (console_state == STATE_DINO) { update_dino(); return; }
  if (console_state == STATE_SURF) { update_surf(); return; }
  if (console_state == STATE_REACT) { update_react(); return; }
  if (console_state == STATE_PARKOUR) { update_parkour(); return; }
}
