/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║                                                                  ║
 * ║        ███████╗██╗    ██╗██╗████████╗ ██████╗██╗  ██╗           ║
 * ║        ██╔════╝██║    ██║██║╚══██╔══╝██╔════╝██║  ██║           ║
 * ║        ███████╗██║ █╗ ██║██║   ██║   ██║     ███████║           ║
 * ║        ╚════██║██║███╗██║██║   ██║   ██║     ██╔══██║           ║
 * ║        ███████║╚███╔███╔╝██║   ██║   ╚██████╗██║  ██║           ║
 * ║        ╚══════╝ ╚══╝╚══╝ ╚═╝   ╚═╝    ╚═════╝╚═╝  ╚═╝          ║
 * ║                                                                  ║
 * ║          G A M I N G   C O N S O L E   v 2 . 0                  ║
 * ║                                                                  ║
 * ║         Blueprint Lucknow · Hack Club · 2026                    ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  HARDWARE CONFIGURATION (hardware-verified):                     ║
 * ║   GPIO2  → Buzzer (+)        GPIO4  → OLED SCL (I2C)            ║
 * ║   GPIO5  → SW1 LEFT          GPIO6  → SW2 RIGHT                  ║
 * ║   GPIO7  → SW3 ACTION (A)    GPIO8  → OLED SDA (I2C)            ║
 * ║                                                                  ║
 * ║   ⚠ Buttons: INPUT_PULLDOWN, active HIGH (pressed = HIGH)        ║
 * ║   I2C OLED Address: 0x3C                                         ║
 * ║   MCU: ESP32-C3 SuperMini                                        ║
 * ║   Display: SSD1306 128×64 OLED                                   ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  GAMES:                                                          ║
 * ║   1. Snake      2. Ping Pong   3. Tic-Tac-Toe                    ║
 * ║   4. T-Rex Run  5. Maze        6. Breakout                       ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  CONTROLS:                                                       ║
 * ║   Snake    : SW1/SW2 = turn L/R (relative)  Hold A = menu       ║
 * ║   Pong     : SW1 = paddle up   SW2 = paddle down                ║
 * ║   TTT      : SW1/SW2 = move cursor   A = place mark             ║
 * ║   T-Rex    : A = jump   SW1 = duck                               ║
 * ║   Maze     : SW1/SW2 = rotate direction   A = step forward       ║
 * ║   Breakout : SW1/SW2 = move paddle   A = launch  Hold A = menu  ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  LIBRARIES REQUIRED:                                             ║
 * ║   · Adafruit SSD1306  v2.5+                                      ║
 * ║   · Adafruit GFX      v1.11+                                     ║
 * ╚══════════════════════════════════════════════════════════════════╝
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

Preferences prefs;

// Forward declarations
void gameInit(uint8_t g);
void gameUpdate();
void gameDraw();

// ─────────────────────────────────────────────────────────────────────────────
//  HARDWARE PIN DEFINITIONS
//  All confirmed via OLED diagnostic tool on the physical board.
// ─────────────────────────────────────────────────────────────────────────────
#define PIN_SDA    8    // OLED I2C Data
#define PIN_SCL    4    // OLED I2C Clock
#define BTN_L      5    // SW1 — LEFT button  (INPUT_PULLDOWN, HIGH when pressed)
#define BTN_R      6    // SW2 — RIGHT button (INPUT_PULLDOWN, HIGH when pressed)
#define BTN_A      7    // SW3 — ACTION btn   (INPUT_PULLDOWN, HIGH when pressed)
#define PIN_BUZ    2    // Passive buzzer positive terminal

#define SCR_W     128   // OLED pixel width
#define SCR_H      64   // OLED pixel height
#define OLED_ADDR 0x3C  // Standard I2C address for SSD1306 128×64

Adafruit_SSD1306 display(SCR_W, SCR_H, &Wire, -1);

// ─────────────────────────────────────────────────────────────────────────────
//  BUTTON INPUT SYSTEM
//
//  Physical setup: Buttons connect GPIO → 10kΩ to GND → Button → 3.3V
//  Mode: INPUT_PULLDOWN  →  unpressed = LOW (0),  pressed = HIGH (1)
//
//  BtnState fields:
//    pL/pR/pA  →  "press" flag — true for exactly ONE frame when button goes HIGH
//    hL/hR/hA  →  "held"  flag — true every frame the button stays HIGH
//    vL/vR/vA  →  previous frame's raw value (used for edge detection)
// ─────────────────────────────────────────────────────────────────────────────
struct BtnState {
  bool pL, pR, pA;   // rising-edge press event (1 frame only)
  bool hL, hR, hA;   // currently held high
  bool vL, vR, vA;   // previous raw readings
} B;

void readBtn() {
  // Read current raw state (HIGH = pressed because INPUT_PULLDOWN)
  bool cL = (digitalRead(BTN_L) == HIGH);
  bool cR = (digitalRead(BTN_R) == HIGH);
  bool cA = (digitalRead(BTN_A) == HIGH);

  // Rising-edge detection: current=HIGH AND previous=LOW → new press event
  B.pL = cL && !B.vL;
  B.pR = cR && !B.vR;
  B.pA = cA && !B.vA;

  // Held state
  B.hL = cL;  B.hR = cR;  B.hA = cA;

  // Store current as previous for next frame
  B.vL = cL;  B.vR = cR;  B.vA = cA;
}

// Returns true if ANY button was pressed this frame
bool anyP() { return B.pL || B.pR || B.pA; }

// ─────────────────────────────────────────────────────────────────────────────
//  NON-BLOCKING SOUND ENGINE
//
//  Uses a circular queue (sq[]) of Note structs.
//  sTick() is called every loop iteration and fires the next note when
//  the previous one has finished — no blocking delay() calls.
//
//  seQ(freq, dur) — enqueue a tone (Hz, milliseconds)
//  sgap(dur)      — enqueue silence (0 Hz = no tone)
// ─────────────────────────────────────────────────────────────────────────────
#define SQ_MAX 24
struct Note { uint16_t freq, dur; };
static Note    sq[SQ_MAX];
static uint8_t sq_h = 0, sq_n = 0;    // head index, count
static uint32_t sq_end = 0;
static bool     sq_playing = false;

void seQ(uint16_t f, uint16_t d) {
  if (sq_n < SQ_MAX) { sq[(sq_h + sq_n) % SQ_MAX] = {f, d}; sq_n++; }
}
void sgap(uint16_t d) { seQ(0, d); }

void sTick() {
  uint32_t now = millis();
  // If note finished, stop buzzer
  if (sq_playing && now >= sq_end) { noTone(PIN_BUZ); sq_playing = false; }
  // If idle and queue has notes, play next one
  if (!sq_playing && sq_n > 0) {
    Note n = sq[sq_h];
    sq_h = (sq_h + 1) % SQ_MAX;
    sq_n--;
    if (n.freq) tone(PIN_BUZ, n.freq, n.dur);
    sq_end = now + n.dur + 5;    // +5 ms gap prevents tones from blurring
    sq_playing = true;
  }
}

// ── Sound effect presets ──────────────────────────────────────────────────────
void sfxSelect()  { seQ(800,30); sgap(8); seQ(1100,45); }
void sfxBack()    { seQ(450,40); }
void sfxEat()     { seQ(1400,20); }
void sfxPoint()   { seQ(660,30); sgap(8); seQ(900,50); }
void sfxDie()     { seQ(440,65); sgap(18); seQ(330,75); sgap(18); seQ(220,160); }
void sfxBounce()  { seQ(580,10); }
void sfxBrick()   { seQ(920,18); }
void sfxJump()    { seQ(500,18); seQ(800,22); }

void sfxWin() {
  const uint16_t n[] = {523,659,784,659,1047};
  const uint16_t d[] = {70,70,70,70,220};
  for (int i = 0; i < 5; i++) { seQ(n[i], d[i]); sgap(8); }
}

void sfxBoot() {
  const uint16_t n[] = {392,523,659,784,1047};
  const uint16_t d[] = {80,80,80,80,200};
  for (int i = 0; i < 5; i++) { seQ(n[i], d[i]); sgap(10); }
}

void sfxType() { seQ(900 + (uint16_t)random(300), 8); }

// ─────────────────────────────────────────────────────────────────────────────
//  PARALLAX STAR FIELD (3 speed layers)
//
//  24 stars split across 3 speeds (1/2/3 px per frame).
//  Slower stars appear further away — simple depth illusion.
// ─────────────────────────────────────────────────────────────────────────────
#define NSTARS 24
static int8_t  stX[NSTARS], stY[NSTARS];
static uint8_t stSpd[NSTARS];

void initStars() {
  for (int i = 0; i < NSTARS; i++) {
    stX[i]   = (int8_t)random(0, SCR_W);
    stY[i]   = (int8_t)random(0, SCR_H);
    stSpd[i] = 1 + (i % 3);          // speeds: 1, 2, or 3
  }
}

void drawStars(bool scroll = true) {
  if (scroll) {
    for (int i = 0; i < NSTARS; i++) {
      stX[i] -= stSpd[i];
      if (stX[i] < 0) { stX[i] = SCR_W - 1; stY[i] = (int8_t)random(0, SCR_H); }
    }
  }
  for (int i = 0; i < NSTARS; i++)
    if (stX[i] >= 0 && stX[i] < SCR_W && stY[i] >= 0 && stY[i] < SCR_H)
      display.drawPixel(stX[i], stY[i], SSD1306_WHITE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  HACK CLUB SWALLOWTAIL FLAG  (48 × 24 px, drawn procedurally)
//
//  Structure:
//    · 2-px wide flagpole on the left
//    · Flag body fills from pole to right edge with a V-notch (swallowtail)
//    · "HC" text printed in BLACK on the white flag body
//    · Two small black squares as decorative accents
// ─────────────────────────────────────────────────────────────────────────────
#define FLAG_W 48
#define FLAG_H 24

void drawHCFlag(int16_t x, int16_t y) {
  // Flagpole (2 px wide, full flag height)
  display.fillRect(x, y, 2, FLAG_H, SSD1306_WHITE);
  // Rivet marks on pole (small black notches)
  for (int8_t r = 2; r < FLAG_H - 2; r += (FLAG_H - 4))
    display.drawFastHLine(x, y + r, 2, SSD1306_BLACK);

  // Swallowtail flag body — notch is 10 px deep, V-apex at row 11 (centre)
  for (int8_t r = 0; r < FLAG_H; r++) {
    int8_t dist  = (int8_t)abs(r - 11);
    int8_t notch = (int8_t)max(0, 10 - (dist * 10) / 11);
    int8_t w     = FLAG_W - 2 - notch;
    if (w > 0) display.drawFastHLine(x + 2, y + r, w, SSD1306_WHITE);
  }

  // "HC" text drawn in BLACK (inverted on white flag)
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(x + 9, y + (FLAG_H / 2) - 4);
  display.print(F("HC"));
  display.setTextColor(SSD1306_WHITE);  // restore

  // Two small decorative accent squares
  display.fillRect(x + 30,  y + 7,  2, 2, SSD1306_BLACK);
  display.fillRect(x + 30, y + 15,  2, 2, SSD1306_BLACK);
}

// ─────────────────────────────────────────────────────────────────────────────
//  BOOT LOGO — "SWITCH" pixel-art letters (drawn with rectangles)
//
//  Each letter is roughly 7 px wide × 9 px tall at text size 1.
//  The logo is drawn at the vertical centre of the screen during boot.
// ─────────────────────────────────────────────────────────────────────────────
void drawSwitchLogo(int16_t x, int16_t y) {
  // Draw a chunky outlined box with "SWITCH" inside, plus a game controller icon
  display.drawRoundRect(x, y, 102, 22, 4, SSD1306_WHITE);
  display.fillRoundRect(x+1, y+1, 100, 20, 3, SSD1306_BLACK);

  // Top highlight line
  display.drawFastHLine(x+4, y+1, 94, SSD1306_WHITE);

  // "SWITCH" in size-2 text
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x + 6, y + 4);
  display.print(F("SWITCH"));
  display.setTextSize(1);

  // Small controller icon to the right
  display.drawRect(x + 87, y + 6, 10, 8, SSD1306_WHITE);
  display.drawPixel(x + 90, y + 8, SSD1306_WHITE);   // D-pad centre
  display.drawPixel(x + 93, y + 9, SSD1306_WHITE);   // button
}

// ─────────────────────────────────────────────────────────────────────────────
//  8×8 GAME ICONS  (stored in PROGMEM flash, not RAM)
//
//  Each icon is 8 bytes = 8 rows of 8 pixels.
//  Bit 7 (MSB) = leftmost pixel in each row.
//  drawIcon() reads them byte-by-byte and plots each set bit as a white pixel.
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t PROGMEM ICO[6][8] = {
  {0x3C,0x42,0x5A,0x5A,0x5A,0x5A,0x42,0x3C},  // 0: Snake — circle with dots
  {0xC0,0xC0,0x18,0x24,0x24,0x18,0x03,0x03},  // 1: Pong  — two paddles + ball
  {0x49,0x00,0xFF,0x00,0x49,0x00,0xFF,0x49},  // 2: TTT   — grid pattern
  {0x0E,0x1F,0x15,0x1F,0x1E,0x3E,0x66,0x24},  // 3: T-Rex — dinosaur silhouette
  {0xFF,0x81,0xBD,0xA5,0xAD,0x81,0xFF,0xFF},  // 4: Maze  — walls
  {0xFF,0xFF,0x00,0x7E,0x00,0x18,0x00,0xFF},  // 5: Breakout — bricks+ball+paddle
};

void drawIcon(int16_t x, int16_t y, uint8_t idx) {
  for (int r = 0; r < 8; r++) {
    uint8_t b = pgm_read_byte(&ICO[idx][r]);
    for (int c = 0; c < 8; c++)
      if (b & (0x80 >> c)) display.drawPixel(x + c, y + r, SSD1306_WHITE);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  STATE MACHINE
//
//  Four global states drive the main loop:
//    ST_BOOT        → animated boot sequence with flag & logo
//    ST_TYPEWRITER  → types out "Blueprint Lucknow / Build Guild 2026"
//    ST_MENU        → 3-card carousel game selector
//    ST_GAME        → active game running
// ─────────────────────────────────────────────────────────────────────────────
enum State : uint8_t { ST_BOOT, ST_TYPEWRITER, ST_MENU, ST_GAME };
static State gState = ST_BOOT;

#define NUM_GAMES 6
static const char* gameList[NUM_GAMES] = {
  "Snake", "Ping Pong", "Tic-Tac-Toe", "T-Rex Run", "Maze", "Breakout"
};
static const char* gameDesc[NUM_GAMES] = {
  "Eat & grow!", "Paddle duel", "3-in-a-row", "Jump!", "Find exit", "Smash brix"
};
static uint8_t  menuSel = 0;
static uint32_t hiScores[NUM_GAMES] = {0};

// ─────────────────────────────────────────────────────────────────────────────
//  BOOT ANIMATION  (3 phases)
//
//  Phase 0: Hack Club flag slides up from below the screen (flagY 68 → 20)
//  Phase 1: "SWITCH CONSOLE" logo flashes 3 times, then subtitle appears
//  Phase 2: Hold everything for 1 second, then advance to typewriter
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t  bootPhase = 0;
static int16_t  flagY     = SCR_H + 4;   // starts below screen
static uint32_t bootTimer = 0;

void bootUpdate() {
  uint32_t now = millis();
  display.clearDisplay();
  drawStars();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (bootPhase == 0) {
    // Slide flag up by 3 px per frame until it reaches y=20
    if (flagY > 20) { flagY -= 3; }
    else            { bootPhase = 1; bootTimer = now; }

    display.setCursor(28, 2);  display.print(F("SWITCH"));
    display.setCursor(70, 2);  display.print(F("v2"));
    display.drawFastHLine(0, 10, SCR_W, SSD1306_WHITE);
    drawHCFlag((SCR_W - FLAG_W) / 2, flagY);

  } else if (bootPhase == 1) {
    // Flash logo 3 times over 900 ms, then hold
    uint32_t t = now - bootTimer;
    bool vis = (t < 200) || (t > 350 && t < 550) || (t > 700);
    display.setCursor(20, 2); display.print(F("SWITCH CONSOLE"));
    if (vis) drawSwitchLogo((SCR_W - 102) / 2, 14);
    if (t > 900) { display.setCursor(8, 52); display.print(F("Gaming Console  v2.0")); }
    if (t > 1700) { bootPhase = 2; bootTimer = now; sfxBoot(); }

  } else {
    // Hold logo on screen, then advance to typewriter after 1 s
    uint32_t t = now - bootTimer;
    display.setCursor(20, 2); display.print(F("SWITCH CONSOLE"));
    drawSwitchLogo((SCR_W - 102) / 2, 14);
    display.setCursor(8, 52); display.print(F("Gaming Console  v2.0"));
    if (t > 1000) gState = ST_TYPEWRITER;
  }
  display.display();
}

// ─────────────────────────────────────────────────────────────────────────────
//  UTILITY: checkBack()
//
//  Call this inside any game's update function.
//  If the player holds the A button for > holdTime ms, returns to main menu.
//  Returns true if menu transition was triggered.
// ─────────────────────────────────────────────────────────────────────────────
bool checkBack(uint16_t holdTime = 700) {
  static uint32_t holdStart = 0;
  if (B.hA) {
    if (!holdStart) holdStart = millis();
    if (millis() - holdStart > holdTime) {
      holdStart = 0;
      sfxBack();
      gState = ST_MENU;
      return true;
    }
  } else {
    holdStart = 0;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  TYPEWRITER SCREEN
//
//  Types out two lines character-by-character at TW_SPEED ms intervals.
//  A blinking block cursor follows the last typed character.
//  After both lines finish, a "Press any button" prompt flashes.
// ─────────────────────────────────────────────────────────────────────────────
static const char TW_L1[] = "Blueprint Lucknow";   // 17 chars
static const char TW_L2[] = "Build Guild 2026";    // 16 chars
#define TW_LEN1  17
#define TW_LEN2  16
#define TW_TOTAL (TW_LEN1 + TW_LEN2)
#define TW_SPEED  68    // ms per character

static uint8_t  twIdx   = 0;
static uint32_t twTimer = 0;

void typewriterUpdate() {
  uint32_t now = millis();
  // Advance one character every TW_SPEED ms
  if (now - twTimer > TW_SPEED && twIdx < TW_TOTAL) {
    twIdx++;
    twTimer = now;
    sfxType();
  }

  display.clearDisplay();
  drawStars(false);                              // stars don't scroll on this screen
  drawHCFlag((SCR_W - FLAG_W) / 2, 1);         // mini flag at top
  display.drawFastHLine(0, 27, SCR_W, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Line 1 — centred horizontally
  uint8_t show1 = min((uint8_t)twIdx, (uint8_t)TW_LEN1);
  int16_t l1x   = (SCR_W - TW_LEN1 * 6) / 2;
  for (uint8_t i = 0; i < show1; i++) {
    display.setCursor(l1x + i * 6, 31);
    display.print(TW_L1[i]);
  }

  // Line 2 — only starts printing after line 1 is complete
  if (twIdx > TW_LEN1) {
    uint8_t show2 = min((uint8_t)(twIdx - TW_LEN1), (uint8_t)TW_LEN2);
    int16_t l2x   = (SCR_W - TW_LEN2 * 6) / 2;
    for (uint8_t i = 0; i < show2; i++) {
      display.setCursor(l2x + i * 6, 41);
      display.print(TW_L2[i]);
    }
  }

  // Blinking block cursor (toggles every 300 ms)
  if (twIdx < TW_TOTAL && (now / 300) % 2 == 0) {
    bool    onL2 = (twIdx >= TW_LEN1);
    uint8_t pos  = onL2 ? (twIdx - TW_LEN1) : twIdx;
    uint8_t len  = onL2 ? TW_LEN2 : TW_LEN1;
    int16_t lx   = (SCR_W - len * 6) / 2;
    int16_t ly   = onL2 ? 41 : 31;
    display.fillRect(lx + pos * 6, ly, 3, 7, SSD1306_WHITE);
  }

  // Press-any-button prompt after typing finishes
  if (twIdx >= TW_TOTAL && (now / 500) % 2) {
    display.setCursor(14, 53);
    display.print(F("[ Press any button ]"));
  }

  display.display();
  if (twIdx >= TW_TOTAL && anyP()) { sfxSelect(); gState = ST_MENU; menuSel = 0; }
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAIN MENU — 3-card carousel
//
//  Layout (128 × 64):
//    Header bar (0–9 px): "SWITCH"  +  hi-score for selected game
//    Left card  (1–28 px wide):   previous game preview
//    Centre card (32–96 px wide): selected game — animated double border
//    Right card (99–127 px wide): next game preview
//    Triangular arrows on left/right edges
// ─────────────────────────────────────────────────────────────────────────────
void menuDraw() {
  display.clearDisplay();
  drawStars();
  uint32_t now = millis();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(2, 0);  display.print(F("SWITCH"));
  display.setCursor(50, 0); display.print(F("Hi:")); display.print(hiScores[menuSel]);
  display.drawFastHLine(0, 9, SCR_W, SSD1306_WHITE);

  uint8_t prev = (menuSel + NUM_GAMES - 1) % NUM_GAMES;
  uint8_t next = (menuSel + 1) % NUM_GAMES;

  // ── Left card (previous game) ──────────────────────────────────────────────
  display.drawRect(1, 11, 28, 52, SSD1306_WHITE);
  display.setCursor(3, 13);
  for (uint8_t i = 0; i < 4 && gameList[prev][i]; i++) display.print(gameList[prev][i]);
  drawIcon(10, 22, prev);

  // ── Right card (next game) ─────────────────────────────────────────────────
  display.drawRect(99, 11, 28, 52, SSD1306_WHITE);
  display.setCursor(101, 13);
  for (uint8_t i = 0; i < 4 && gameList[next][i]; i++) display.print(gameList[next][i]);
  drawIcon(108, 22, next);

  // ── Centre card (selected game) ── animated pulsing double border ──────────
  if ((now / 250) % 2) display.drawRect(31, 10, 66, 54, SSD1306_WHITE);
  display.drawRect(32, 11, 64, 52, SSD1306_WHITE);
  display.fillRect(33, 12, 62, 50, SSD1306_BLACK);    // clear interior

  // Index badge (inverted: white bg, black text)
  display.fillRect(33, 12, 22, 9, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(35, 13);
  display.print(menuSel + 1); display.print(F("/6"));

  // Icon, name, description inside centre card
  drawIcon(63, 17, menuSel);
  display.setTextColor(SSD1306_WHITE);
  uint8_t nl = strlen(gameList[menuSel]);
  display.setCursor(33 + (62 - nl * 6) / 2, 28);
  display.print(gameList[menuSel]);
  uint8_t dl = strlen(gameDesc[menuSel]);
  display.setCursor(33 + (62 - dl * 6) / 2, 38);
  display.print(gameDesc[menuSel]);
  display.setCursor(37, 53); display.print(F("< [A]Play >"));

  // ── Navigation arrows ──────────────────────────────────────────────────────
  display.fillTriangle(  3, 37,  9, 31,  9, 43, SSD1306_WHITE);   // left
  display.fillTriangle(124, 37, 118, 31, 118, 43, SSD1306_WHITE);  // right

  display.display();
}

void menuUpdate() {
  if (B.pL) { menuSel = (menuSel + NUM_GAMES - 1) % NUM_GAMES; sfxBack(); }
  if (B.pR) { menuSel = (menuSel + 1) % NUM_GAMES; sfxSelect(); }
  if (B.pA) { sfxSelect(); gState = ST_GAME; gameInit(menuSel); }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SHARED GAME STATE
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t  activeGame = 0;
static bool     gameOver   = false;
static uint32_t curScore   = 0;
static uint32_t goTimer    = 0;

// ─────────────────────────────────────────────────────────────────────────────
//  GAME-OVER OVERLAY
//
//  Slides in from top of screen over ~96 ms.
//  Shows score, high score, and retry/menu prompt.
//  The overlay is drawn ON TOP of the game's own drawing each frame.
// ─────────────────────────────────────────────────────────────────────────────
void drawGameOver() {
  uint32_t now = millis();
  uint8_t  oy  = (uint8_t)min(8UL, (now - goTimer) / 12UL);   // slides from top
  display.fillRect(12, oy + 5,  104, 46, SSD1306_BLACK);        // erase area
  display.drawRect(12, oy + 5,  104, 46, SSD1306_WHITE);        // outer border
  display.drawRect(13, oy + 6,  102, 44, SSD1306_WHITE);        // inner border
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(22, oy + 10); display.print(F("** GAME  OVER **"));
  display.drawFastHLine(13, oy + 19, 102, SSD1306_WHITE);
  display.setCursor(18, oy + 22); display.print(F("Score: ")); display.print(curScore);
  display.setCursor(18, oy + 31); display.print(F("Best : ")); display.print(hiScores[activeGame]);
  if ((now / 450) % 2) {
    display.setCursor(13, oy + 41);
    display.print(F("[A]Retry  [LR]Menu"));
  }
}

// Saves hi-score to NVS flash via Preferences library
void saveHiScore() {
  if (curScore > hiScores[activeGame]) {
    hiScores[activeGame] = curScore;
    prefs.putUInt(("g" + String(activeGame)).c_str(), curScore);
  }
}

// ═════════════════════════════════════════════════════════════════════════════
//  GAME 1 — SNAKE
//
//  Classic snake: eat food diamonds to grow, avoid walls and your own body.
//
//  Controls (relative steering):
//    SW1 (LEFT)  → rotate direction 90° counter-clockwise
//    SW2 (RIGHT) → rotate direction 90° clockwise
//    Hold A      → return to menu (after 800 ms)
//
//  Mechanics:
//    · Speed increases by 5 ms per food eaten (min speed 70 ms/step)
//    · Score +10 per food
//    · Grid: 32 × 13 cells at 4 px each (fits 128×62 play area)
// ═════════════════════════════════════════════════════════════════════════════
#define SN_CELL  4
#define SN_COLS (SCR_W / SN_CELL)          // 32 columns
#define SN_ROWS ((SCR_H - 10) / SN_CELL)   // 13 rows (below 10-px header)
#define SN_MAX   110                        // max snake length

struct SnCell { int8_t x, y; };
static SnCell   snBody[SN_MAX];
static uint8_t  snLen;
static int8_t   snDx, snDy;
static SnCell   snFood;
static uint16_t snSpd;
static uint32_t snLast, snHold;

// Place food at a random cell not occupied by the snake body
void snPlace() {
  bool ok;
  do {
    ok = true;
    snFood = { (int8_t)random(SN_COLS), (int8_t)random(SN_ROWS) };
    for (int i = 0; i < snLen; i++)
      if (snBody[i].x == snFood.x && snBody[i].y == snFood.y) { ok = false; break; }
  } while (!ok);
}

void snakeInit() {
  snLen = 3; snDx = 1; snDy = 0;
  curScore = 0; snSpd = 210; snHold = 0;
  for (int i = 0; i < snLen; i++)
    snBody[i] = { (int8_t)(SN_COLS / 2 - i), (int8_t)(SN_ROWS / 2) };
  snPlace();
  snLast = millis();
}

void snakeUpdate() {
  // Hold A for 800 ms → back to menu
  if (B.hA) { if (!snHold) snHold = millis(); if (millis() - snHold > 800) { gState = ST_MENU; return; } }
  else snHold = 0;

  // Relative steering: rotate the (dx,dy) direction vector
  // CCW rotation: (dx,dy) → (dy,−dx)    CW rotation: (dx,dy) → (−dy,dx)
  if (B.pL) { int8_t t = snDx; snDx =  snDy; snDy = -t; }
  if (B.pR) { int8_t t = snDx; snDx = -snDy; snDy =  t; }

  // Only step the snake once per snSpd ms
  uint32_t now = millis();
  if (now - snLast < snSpd) return;
  snLast = now;

  // Calculate new head position
  SnCell nh = { (int8_t)(snBody[0].x + snDx), (int8_t)(snBody[0].y + snDy) };

  // Wall collision → game over
  if (nh.x < 0 || nh.x >= SN_COLS || nh.y < 0 || nh.y >= SN_ROWS) {
    sfxDie(); saveHiScore(); gameOver = true; goTimer = millis(); return;
  }
  // Self-collision (skip tail tip — it moves away this same frame)
  for (int i = 0; i < snLen - 1; i++) {
    if (snBody[i].x == nh.x && snBody[i].y == nh.y) {
      sfxDie(); saveHiScore(); gameOver = true; goTimer = millis(); return;
    }
  }

  // Check if new head lands on food
  bool ate = (nh.x == snFood.x && nh.y == snFood.y);
  if (ate && snLen < SN_MAX) snLen++;

  // Shift body array: each cell copies the one in front of it
  for (int i = snLen - 1; i > 0; i--) snBody[i] = snBody[i - 1];
  snBody[0] = nh;

  if (ate) {
    curScore += 10;
    snSpd = (uint16_t)max(70, (int)snSpd - 5);   // speed up, floor at 70 ms
    sfxEat();
    snPlace();
  }
}

void snakeDraw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0,  0); display.print(F("SNAKE"));
  display.setCursor(42, 0); display.print(F("Sc:")); display.print(curScore);
  display.setCursor(90, 0); display.print(F("Hi:")); display.print(hiScores[0]);
  display.drawFastHLine(0, 9, SCR_W, SSD1306_WHITE);

  // Food drawn as a small diamond (3 pixels)
  int fx = snFood.x * SN_CELL, fy = snFood.y * SN_CELL + 10;
  display.drawPixel(fx + 2, fy,     SSD1306_WHITE);
  display.drawLine( fx + 1, fy + 1, fx + 3, fy + 1, SSD1306_WHITE);
  display.drawPixel(fx + 2, fy + 2, SSD1306_WHITE);

  // Snake body: head is full 4×4 square; body segments are 2×2 squares centred in cell
  for (int i = 0; i < snLen; i++) {
    int x = snBody[i].x * SN_CELL, y = snBody[i].y * SN_CELL + 10;
    if (i == 0) display.fillRect(x, y, SN_CELL, SN_CELL, SSD1306_WHITE);       // head
    else        display.fillRect(x + 1, y + 1, SN_CELL - 2, SN_CELL - 2, SSD1306_WHITE); // body
  }
  if (gameOver) drawGameOver();
  display.display();
}

// ═════════════════════════════════════════════════════════════════════════════
//  GAME 2 — PING PONG
//
//  1-vs-AI pong. Player controls the left paddle; right paddle is AI-driven.
//
//  Controls:
//    SW1 (LEFT)  → move player paddle UP
//    SW2 (RIGHT) → move player paddle DOWN
//
//  AI behaviour:
//    · When ball moves toward AI (ppBDX > 0): tracks ball Y (max 2 px/frame)
//    · When ball moves away: drifts back to screen centre (recovers)
//    · This makes the AI beatable — it can't always react fast enough at high speed
//
//  Ball mechanics:
//    · Reflection angle varies by where ball hits the paddle (centre = flat, edge = steep)
//    · Speed (ppSpd) increases by 0.18 per rally hit, max 5.5 px/frame
//    · Win condition: first to PP_WIN (7) points
// ═════════════════════════════════════════════════════════════════════════════
#define PP_PH    16    // paddle height (pixels)
#define PP_PW     3    // paddle width
#define PP_PS     2    // paddle speed (px per frame)
#define PP_WIN    7    // points to win a match
#define PP_SPD0   2.0f // initial ball speed

static float    ppBX, ppBY, ppBDX, ppBDY, ppSpd;
static int16_t  ppP1Y, ppP2Y;   // paddle Y centres (player, AI)
static uint8_t  ppP1S, ppP2S;   // scores
static uint32_t ppLast;

void pongInit() {
  ppBX = SCR_W / 2; ppBY = SCR_H / 2;
  ppBDX = -PP_SPD0; ppBDY = 0.9f;
  ppP1Y = SCR_H / 2; ppP2Y = SCR_H / 2;
  ppP1S = ppP2S = 0;
  curScore = 0; ppSpd = PP_SPD0;
  ppLast = millis();
}

void pongUpdate() {
  uint32_t now = millis();
  if (now - ppLast < 22) return;   // ~45 FPS cap for pong physics
  ppLast = now;

  // Player paddle movement (hold buttons)
  if (B.hL && ppP1Y > PP_PH / 2 + 10)        ppP1Y -= PP_PS;
  if (B.hR && ppP1Y < SCR_H - PP_PH / 2 - 1) ppP1Y += PP_PS;

  // AI paddle movement (lag-based, intentionally beatable)
  if (ppBDX > 0) {                          // ball approaching AI
    if (ppP2Y < ppBY - 2) ppP2Y += 2;
    if (ppP2Y > ppBY + 2) ppP2Y -= 2;
  } else {                                  // ball moving away — drift to centre
    if (ppP2Y < SCR_H / 2 - 2) ppP2Y += 1;
    if (ppP2Y > SCR_H / 2 + 2) ppP2Y -= 1;
  }

  // Move ball
  ppBX += ppBDX; ppBY += ppBDY;

  // Top/bottom wall bounces
  if (ppBY <= 10.5f) { ppBY = 10.5f; ppBDY =  fabsf(ppBDY); sfxBounce(); }
  if (ppBY >= SCR_H - 1.5f) { ppBY = SCR_H - 1.5f; ppBDY = -fabsf(ppBDY); sfxBounce(); }

  // Player paddle hit (left side)
  if (ppBX <= PP_PW + 5 && ppBX >= PP_PW + 1 &&
      ppBY >= ppP1Y - PP_PH / 2 && ppBY <= ppP1Y + PP_PH / 2) {
    float rel = (ppBY - ppP1Y) / (PP_PH / 2.0f);  // –1..+1 relative position
    ppBDY = rel * 2.8f;
    ppSpd = min(ppSpd + 0.18f, 5.5f);
    ppBDX = ppSpd;
    sfxBounce(); sfxPoint(); curScore++;
  }
  // AI paddle hit (right side)
  if (ppBX >= SCR_W - PP_PW - 5 && ppBX <= SCR_W - PP_PW - 1 &&
      ppBY >= ppP2Y - PP_PH / 2 && ppBY <= ppP2Y + PP_PH / 2) {
    float rel = (ppBY - ppP2Y) / (PP_PH / 2.0f);
    ppBDY = rel * 2.8f;
    ppSpd = min(ppSpd + 0.12f, 5.5f);
    ppBDX = -ppSpd;
    sfxBounce();
  }

  // Ball exits left → AI scores
  if (ppBX < 0) {
    ppP2S++; ppBX = SCR_W / 2; ppBY = SCR_H / 2;
    ppBDX = -PP_SPD0; ppBDY = random(2) ? 0.9f : -0.9f;
    ppSpd = PP_SPD0; sfxDie();
  }
  // Ball exits right → player scores
  if (ppBX > SCR_W) {
    ppP1S++; ppBX = SCR_W / 2; ppBY = SCR_H / 2;
    ppBDX = PP_SPD0; ppBDY = random(2) ? 0.9f : -0.9f;
    ppSpd = PP_SPD0; sfxPoint();
  }

  // Win condition check
  if (ppP2S >= PP_WIN) {
    curScore = (uint32_t)ppP1S * 10;
    sfxDie(); gameOver = true; goTimer = millis(); saveHiScore();
  }
  if (ppP1S >= PP_WIN) {
    curScore = (uint32_t)ppP1S * 10 + 100;   // bonus for winning
    sfxWin(); gameOver = true; goTimer = millis(); saveHiScore();
  }
}

void pongDraw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0,  0); display.print(F("PONG"));
  display.setCursor(40, 0); display.print(ppP1S); display.print(F(" : ")); display.print(ppP2S);
  display.setCursor(82, 0); display.print(F("L/R=move"));
  display.drawFastHLine(0, 9, SCR_W, SSD1306_WHITE);
  // Centre dividing line (dashed)
  for (int y = 10; y < SCR_H; y += 5) display.drawPixel(SCR_W / 2, y, SSD1306_WHITE);
  // Paddles
  display.fillRect(2, ppP1Y - PP_PH / 2, PP_PW, PP_PH, SSD1306_WHITE);              // player
  display.fillRect(SCR_W - PP_PW - 2, ppP2Y - PP_PH / 2, PP_PW, PP_PH, SSD1306_WHITE); // AI
  // Ball (3×3 square)
  display.fillRect((int)ppBX - 1, (int)ppBY - 1, 3, 3, SSD1306_WHITE);
  // Speed bar at top-right (up to 28 px wide)
  uint8_t spBar = (uint8_t)((ppSpd - PP_SPD0) / (5.5f - PP_SPD0) * 28.0f);
  if (spBar > 0) display.fillRect(96, 1, spBar, 4, SSD1306_WHITE);
  if (gameOver) drawGameOver();
  display.display();
}

// ═════════════════════════════════════════════════════════════════════════════
//  GAME 3 — TIC-TAC-TOE
//
//  Player (X) vs unbeatable Minimax AI (O).
//  A 500 ms artificial "thinking" delay makes it feel more natural.
//
//  Controls:
//    SW1 → move cursor left  (wraps 0–8)
//    SW2 → move cursor right (wraps 0–8)
//    A   → place mark on current cell
//
//  Scoring: Win = 150 pts, Lose/Draw = 0
// ═════════════════════════════════════════════════════════════════════════════
static int8_t   tttB[9];         // board: 0=empty 1=player 2=AI
static uint8_t  tttCur;          // cursor position (0–8)
static bool     tttPTurn;        // true = player's turn
static uint8_t  tttWin;          // 0=ongoing 1=player 2=AI 3=draw
static uint32_t tttAiTimer;

// All 8 winning lines (3 cells each)
static const uint8_t PROGMEM tttLines[8][3] = {
  {0,1,2},{3,4,5},{6,7,8},    // rows
  {0,3,6},{1,4,7},{2,5,8},    // columns
  {0,4,8},{2,4,6}             // diagonals
};

// Check board for a winner. Returns: 0=none 1=player 2=AI 3=draw
uint8_t tttCheck() {
  for (int i = 0; i < 8; i++) {
    uint8_t a = pgm_read_byte(&tttLines[i][0]);
    uint8_t b = pgm_read_byte(&tttLines[i][1]);
    uint8_t c = pgm_read_byte(&tttLines[i][2]);
    if (tttB[a] && tttB[a] == tttB[b] && tttB[b] == tttB[c]) return tttB[a];
  }
  for (int i = 0; i < 9; i++) if (!tttB[i]) return 0;   // empty cell exists
  return 3;   // board full, no winner = draw
}

// Minimax recursive search. mx=true means AI's turn (maximise score)
int tttMM(int8_t* bd, bool mx) {
  uint8_t w = tttCheck();
  if (w == 2) return 10;    // AI wins
  if (w == 1) return -10;   // player wins
  if (w == 3) return 0;     // draw
  int best = mx ? -100 : 100;
  for (int i = 0; i < 9; i++) if (!bd[i]) {
    bd[i] = mx ? 2 : 1;
    int s = tttMM(bd, !mx);
    bd[i] = 0;
    best = mx ? max(best, s) : min(best, s);
  }
  return best;
}

// Find the best move for AI (returns cell index 0–8)
uint8_t tttBest() {
  int best = -100; uint8_t mv = 0;
  for (int i = 0; i < 9; i++) if (!tttB[i]) {
    tttB[i] = 2;
    int s = tttMM(tttB, false);
    tttB[i] = 0;
    if (s > best) { best = s; mv = i; }
  }
  return mv;
}

void tttInit() {
  memset(tttB, 0, 9); tttCur = 4;   // start cursor at centre cell
  tttPTurn = true; tttWin = 0; curScore = 0; tttAiTimer = 0;
}

void tttUpdate() {
  if (tttWin) {   // game ended — wait for button to restart or quit
    if (anyP()) {
      if (B.pA)         { tttInit(); gameOver = false; return; }
      if (B.pL || B.pR) { gState = ST_MENU; gameOver = false; return; }
    }
    return;
  }
  if (tttPTurn) {
    if (B.pL) { tttCur = (tttCur + 8) % 9; sfxBack(); }
    if (B.pR) { tttCur = (tttCur + 1) % 9; sfxSelect(); }
    if (B.pA && !tttB[tttCur]) {       // only place if cell is empty
      tttB[tttCur] = 1; sfxEat();
      tttWin = tttCheck();
      if (tttWin) { curScore = (tttWin == 1) ? 150 : 0; saveHiScore(); return; }
      tttPTurn = false; tttAiTimer = millis();
    }
  } else {
    // AI "thinks" for 500 ms before placing
    if (millis() - tttAiTimer > 500) {
      uint8_t m = tttBest(); tttB[m] = 2; sfxBounce();
      tttWin = tttCheck();
      if (tttWin) { curScore = (tttWin == 1) ? 150 : 0; saveHiScore(); }
      tttPTurn = true;
    }
  }
}

void tttDraw() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0, 0); display.print(F("TIC-TAC-TOE  X=You O=AI"));
  display.drawFastHLine(0, 9, SCR_W, SSD1306_WHITE);

  // 3×3 grid at x=35, y=11, each cell = 17 px
  const int ox = 35, oy = 11, cs = 17;
  display.drawFastVLine(ox + cs,     oy, cs * 3, SSD1306_WHITE);
  display.drawFastVLine(ox + cs * 2, oy, cs * 3, SSD1306_WHITE);
  display.drawFastHLine(ox, oy + cs,     cs * 3, SSD1306_WHITE);
  display.drawFastHLine(ox, oy + cs * 2, cs * 3, SSD1306_WHITE);

  for (int i = 0; i < 9; i++) {
    int cx = ox + (i % 3) * cs + cs / 2;
    int cy = oy + (i / 3) * cs + cs / 2;
    // Cursor highlight: dotted box around selected cell
    if (i == tttCur && tttPTurn && !tttWin)
      display.drawRect(ox + (i % 3) * cs + 1, oy + (i / 3) * cs + 1, cs - 1, cs - 1, SSD1306_WHITE);
    if      (tttB[i] == 1) {   // player X
      display.drawLine(cx-5,cy-5, cx+5,cy+5, SSD1306_WHITE);
      display.drawLine(cx+5,cy-5, cx-5,cy+5, SSD1306_WHITE);
    } else if (tttB[i] == 2) { // AI O
      display.drawCircle(cx, cy, 6, SSD1306_WHITE);
    }
  }

  // Status sidebar (left of grid)
  display.setCursor(0, 12);
  if      (!tttWin)     display.print(tttPTurn ? F("Your") : F("AI.."));
  else if (tttWin == 1) display.print(F("WIN!"));
  else if (tttWin == 2) display.print(F("Lost"));
  else                  display.print(F("Draw"));
  display.setCursor(0, 24); display.print(F("<>mv"));
  display.setCursor(0, 34); display.print(F("[A]ok"));
  if (tttWin) {
    display.setCursor(0, 44); display.print(F("[A]+"));
    display.setCursor(0, 53); display.print(F("[LR]-"));
  }
  display.display();
}

// ═════════════════════════════════════════════════════════════════════════════
//  GAME 4 — T-REX RUNNER
//
//  Endless runner: dodge cacti and birds, get as far as possible.
//
//  Controls:
//    A   → jump (only when on ground)
//    SW1 → duck (reduces hitbox height; hold while grounded)
//
//  Mechanics:
//    · Gravity pulls the dino down at 0.48 px/frame²
//    · Jump gives initial upward velocity of −6.0 px/frame
//    · Bird obstacles appear after 200 pts, chosen randomly (25% chance)
//    · Game speed (txSpd) increases 1 px/frame every 90 pts
//    · Score +10 for each obstacle cleared
// ═════════════════════════════════════════════════════════════════════════════
#define TX_GND   53    // Y coordinate of ground line
#define TX_X     18    // X position of the dinosaur (fixed horizontally)
#define TX_W     12    // dinosaur width
#define TX_H_N   12    // normal standing height
#define TX_H_D    6    // ducking height

static float    txY, txVY;              // dino Y position and vertical velocity
static bool     txGround, txDuck;       // on ground? ducking?
static uint32_t txLast, txObsT, txObsI;
static uint16_t txSpd;                  // obstacle scroll speed (px per update)
static uint32_t txScr;                  // current score
static uint8_t  txFr;                   // animation frame (0 or 1)
static uint32_t txFrT;                  // last frame switch time
static int16_t  txCX[3]; static uint8_t txCY[3];  // cloud positions

// Obstacle: x position, type (0=cactus 1=bird), active flag
struct TxObs { int16_t x; uint8_t t; bool on; };
static TxObs txObs[3];

// T-Rex sprite A — walking frame 1 (12 cols × 12 rows, 2 bytes per row = MSB first)
static const uint8_t PROGMEM TREX_A[] = {
  0x07,0xC0, 0x07,0xC0, 0x07,0x80, 0x07,0xE0,
  0x7F,0xC0, 0xFF,0x80, 0xF8,0x00, 0xFC,0x00,
  0x7C,0x00, 0x38,0x00, 0x28,0x00, 0x38,0x00,
};
// T-Rex sprite B — walking frame 2
static const uint8_t PROGMEM TREX_B[] = {
  0x07,0xC0, 0x07,0xC0, 0x07,0x80, 0x07,0xE0,
  0x7F,0xC0, 0xFF,0x80, 0xF8,0x00, 0xFC,0x00,
  0x7C,0x00, 0x38,0x00, 0x10,0x00, 0x30,0x00,
};
// Cactus obstacle (8 wide × 12 tall, 1 byte per row)
static const uint8_t PROGMEM CACTUS[] = {
  0x18,0x18,0x9A,0xDB,0xFF,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,
};
// Bird obstacle (12 wide × 8 tall, 2 bytes per row)
static const uint8_t PROGMEM BIRD[] = {
  0x0E,0x00, 0x1F,0x80, 0x7F,0xE0, 0xFF,0xF0,
  0x7F,0xE0, 0x3F,0xC0, 0x1C,0x00, 0x08,0x00,
};

void txSpawn() {
  for (int i = 0; i < 3; i++) if (!txObs[i].on) {
    txObs[i] = {
      (int16_t)(SCR_W + 12),
      (uint8_t)(txScr > 200 && random(4) == 0 ? 1 : 0),  // 25% bird chance after 200 pts
      true
    };
    break;
  }
}

void trexInit() {
  txY = TX_GND - TX_H_N; txVY = 0; txGround = true; txDuck = false;
  txScr = 0; txSpd = 2; txFr = 0; txFrT = 0;
  for (int i = 0; i < 3; i++) txObs[i].on = false;
  txObsI = 1800;    // initial obstacle interval (ms)
  txObsT = millis();
  for (int i = 0; i < 3; i++) { txCX[i] = (int16_t)random(20, SCR_W); txCY[i] = 12 + random(16); }
  curScore = 0; txLast = millis();
}

void trexUpdate() {
  uint32_t now = millis();
  if (now - txLast < 24) return;    // ~42 FPS update cap
  txLast = now;

  // Jump when A pressed and dino is on the ground
  if (B.pA && txGround) { txVY = -6.0f; txGround = false; sfxJump(); }
  txDuck = B.hL && txGround;        // hold SW1 to duck (ground only)

  // Apply gravity and integrate velocity
  txVY += 0.48f;
  txY  += txVY;
  if (txY >= TX_GND - TX_H_N) { txY = TX_GND - TX_H_N; txVY = 0; txGround = true; }

  // Animate legs every 120 ms
  if (now - txFrT > 120) { txFr ^= 1; txFrT = now; }

  // Spawn new obstacles on interval (shrinks over time)
  if (now - txObsT > txObsI) {
    txObsT = now; txSpawn();
    txObsI = (uint16_t)max(550, (int)txObsI - 18);   // minimum 550 ms gap
  }

  // Move obstacles and check AABB collision
  int8_t  rH = txDuck ? TX_H_D : TX_H_N;
  int16_t rY = txDuck ? (TX_GND - TX_H_D) : (int16_t)txY;

  for (int i = 0; i < 3; i++) {
    if (!txObs[i].on) continue;
    txObs[i].x -= txSpd;
    if (txObs[i].x < -16) { txObs[i].on = false; txScr += 10; sfxEat(); continue; }

    // Obstacle bounding box
    int16_t oX = txObs[i].x;
    int8_t  oY = (txObs[i].t == 0) ? (TX_GND - 12) : (TX_GND - 22);
    int8_t  oH = (txObs[i].t == 0) ? 12 : 8;

    // AABB overlap test with small erosion on dino box for fairness
    if (TX_X + 2 + TX_W - 4 > oX + 1 &&
        TX_X + 2             < oX + 10 &&
        rY + rH               > oY &&
        rY                    < oY + oH) {
      curScore = txScr; sfxDie(); gameOver = true; goTimer = millis(); saveHiScore(); return;
    }
  }

  txSpd = 2 + (uint16_t)(txScr / 90);   // speed up 1 px every 90 pts

  // Scroll clouds
  for (int i = 0; i < 3; i++) {
    txCX[i]--;
    if (txCX[i] < -20) { txCX[i] = SCR_W + 8; txCY[i] = 12 + random(16); }
  }
  curScore = txScr;
}

void trexDraw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0, 0); display.print(F("T-REX  Sc:")); display.print(txScr);
  display.drawFastHLine(0, 9, SCR_W, SSD1306_WHITE);

  // Ground line + scrolling pebble texture
  display.drawFastHLine(0, TX_GND, SCR_W, SSD1306_WHITE);
  for (int x = 0; x < SCR_W; x += 8)
    display.drawPixel((x + (int)(txScr / 2)) % SCR_W, TX_GND + 1, SSD1306_WHITE);

  // Clouds (simple 2-line shapes)
  for (int i = 0; i < 3; i++) {
    display.drawLine(txCX[i], txCY[i], txCX[i] + 12, txCY[i], SSD1306_WHITE);
    display.drawLine(txCX[i] + 2, txCY[i] - 2, txCX[i] + 10, txCY[i] - 2, SSD1306_WHITE);
  }

  // Dinosaur sprite (select rows based on duck state)
  const uint8_t* spr = txFr ? TREX_B : TREX_A;
  int8_t sRow = txDuck ? (TX_H_N - TX_H_D) : 0;
  int8_t sH   = txDuck ? TX_H_D : TX_H_N;
  for (int r = 0; r < sH; r++) {
    uint8_t b1 = pgm_read_byte(spr + (sRow + r) * 2);
    uint8_t b2 = pgm_read_byte(spr + (sRow + r) * 2 + 1);
    for (int c = 0; c < 8; c++)
      if (b1 & (0x80 >> c)) display.drawPixel(TX_X + c, (int)txY + r, SSD1306_WHITE);
    for (int c = 0; c < 4; c++)
      if (b2 & (0x80 >> c)) display.drawPixel(TX_X + 8 + c, (int)txY + r, SSD1306_WHITE);
  }

  // Obstacles
  for (int i = 0; i < 3; i++) {
    if (!txObs[i].on) continue;
    if (txObs[i].t == 0) {   // cactus
      for (int r = 0; r < 12; r++) {
        uint8_t b = pgm_read_byte(CACTUS + r);
        for (int c = 0; c < 8; c++)
          if (b & (0x80 >> c)) display.drawPixel(txObs[i].x + c, TX_GND - 12 + r, SSD1306_WHITE);
      }
    } else {                  // bird
      for (int r = 0; r < 8; r++) {
        uint8_t b1 = pgm_read_byte(BIRD + r*2), b2 = pgm_read_byte(BIRD + r*2+1);
        for (int c = 0; c < 8; c++) if (b1&(0x80>>c)) display.drawPixel(txObs[i].x+c, TX_GND-22+r, SSD1306_WHITE);
        for (int c = 0; c < 4; c++) if (b2&(0x80>>c)) display.drawPixel(txObs[i].x+8+c, TX_GND-22+r, SSD1306_WHITE);
      }
    }
  }

  display.setCursor(66, 12); display.print(F("[A]jump [L]duck"));
  if (gameOver) drawGameOver();
  display.display();
}

// ═════════════════════════════════════════════════════════════════════════════
//  GAME 5 — MAZE
//
//  Navigate from top-left (0,0) to bottom-right (13,5).
//  Direction-based controls (you face a direction, then step forward).
//
//  Controls:
//    SW1 → rotate facing direction 90° CCW (left turn)
//    SW2 → rotate facing direction 90° CW  (right turn)
//    A   → step one cell forward in current direction
//    Hold A > 800 ms → menu (via hold-repeat system)
//
//  Grid: 14 cols × 6 rows, 7 px per cell → 98×42 px play area
//  Score = max(0, 500 − moves × 4)  — finish faster for higher score
// ═════════════════════════════════════════════════════════════════════════════
#define MZ_C    14    // grid columns
#define MZ_R     6    // grid rows
#define MZ_T     7    // tile size in pixels
#define MZ_OX   10    // screen X offset
#define MZ_OY   11    // screen Y offset (below header bar)

// 1=wall, 0=open path
static const uint8_t PROGMEM MZ_MAP[MZ_R][MZ_C] = {
  {0,0,1,0,0,0,1,0,0,1,0,0,0,0},
  {1,0,0,0,1,0,0,0,1,0,0,1,0,0},
  {0,0,1,1,0,0,1,0,0,0,1,0,0,0},
  {0,1,0,0,0,1,0,0,1,0,0,0,1,0},
  {0,0,0,1,0,0,0,1,0,0,0,1,0,0},
  {1,0,1,0,1,0,0,0,1,0,1,0,0,0},
};

// Direction vectors: N=0 E=1 S=2 W=3
static const int8_t MZ_DX[] = { 0, 1, 0,-1 };
static const int8_t MZ_DY[] = {-1, 0, 1, 0 };
static const char   MZ_DC[] = "NESW";

static uint8_t  mzG[MZ_R][MZ_C];       // local copy of map (in RAM)
static uint8_t  mzPX, mzPY, mzDir;     // player position and facing direction
static uint32_t mzMoves, mzMvT, mzHold;

void mazeInit() {
  for (int r = 0; r < MZ_R; r++)
    for (int c = 0; c < MZ_C; c++)
      mzG[r][c] = pgm_read_byte(&MZ_MAP[r][c]);
  mzPX = 0; mzPY = 0; mzDir = 1;   // start at (0,0) facing East
  mzMoves = 0; mzMvT = 0; mzHold = 0; curScore = 0;
}

void mazeUpdate() {
  // Rotation (instant, no hold needed)
  if (B.pL) { mzDir = (mzDir + 3) % 4; sfxBack(); return; }
  if (B.pR) { mzDir = (mzDir + 1) % 4; sfxSelect(); return; }

  // Step forward (hold A for auto-repeat at 220 ms intervals)
  uint32_t now = millis();
  if (B.hA) {
    if (!mzHold) mzHold = now;
    if (now - mzMvT < 220) return;
    mzMvT = now;
    int8_t nx = (int8_t)mzPX + MZ_DX[mzDir];
    int8_t ny = (int8_t)mzPY + MZ_DY[mzDir];
    if (nx >= 0 && nx < MZ_C && ny >= 0 && ny < MZ_R && !mzG[ny][nx]) {
      mzPX = nx; mzPY = ny; mzMoves++; sfxEat();
      if (mzPX == MZ_C - 1 && mzPY == MZ_R - 1) {   // reached exit!
        curScore = (uint32_t)max(0L, (long)(500 - (long)mzMoves * 4));
        sfxWin(); gameOver = true; goTimer = millis(); saveHiScore();
      }
    } else sfxBounce();   // hit a wall
  } else mzHold = 0;
}

void mazeDraw() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0,  0); display.print(F("MAZE"));
  display.setCursor(36, 0); display.print(F("Mv:")); display.print(mzMoves);
  display.setCursor(82, 0); display.print(F("Dir:")); display.print(MZ_DC[mzDir]);
  display.drawFastHLine(0, 9, SCR_W, SSD1306_WHITE);

  // Draw wall cells as filled rectangles
  for (int r = 0; r < MZ_R; r++)
    for (int c = 0; c < MZ_C; c++)
      if (mzG[r][c])
        display.fillRect(MZ_OX + c * MZ_T, MZ_OY + r * MZ_T, MZ_T, MZ_T, SSD1306_WHITE);

  // Exit marker: star shape at bottom-right cell centre
  int ex = MZ_OX + (MZ_C - 1) * MZ_T + MZ_T / 2;
  int ey = MZ_OY + (MZ_R - 1) * MZ_T + MZ_T / 2;
  display.drawLine(ex-3, ey, ex+3, ey, SSD1306_WHITE);
  display.drawLine(ex, ey-3, ex, ey+3, SSD1306_WHITE);
  display.drawLine(ex-2, ey-2, ex+2, ey+2, SSD1306_WHITE);
  display.drawLine(ex+2, ey-2, ex-2, ey+2, SSD1306_WHITE);

  // Player: filled circle with a directional notch (erased pixels)
  int px = MZ_OX + mzPX * MZ_T + MZ_T / 2;
  int py = MZ_OY + mzPY * MZ_T + MZ_T / 2;
  display.fillCircle(px, py, 2, SSD1306_WHITE);
  display.drawPixel(px + MZ_DX[mzDir] * 2, py + MZ_DY[mzDir] * 2, SSD1306_BLACK);
  display.drawPixel(px + MZ_DX[mzDir],     py + MZ_DY[mzDir],     SSD1306_BLACK);

  display.setCursor(0, 57); display.print(F("<>turn  [A]step fwd"));
  if (gameOver) drawGameOver();
  display.display();
}

// ═════════════════════════════════════════════════════════════════════════════
//  GAME 6 — BREAKOUT
//
//  Classic brick-breaker. Survive each level (all bricks cleared = new level).
//
//  Controls:
//    SW1       → move paddle LEFT
//    SW2       → move paddle RIGHT
//    A         → launch ball (pre-launch, ball sits on paddle)
//    Hold A    → back to menu (after 700 ms)
//
//  Mechanics:
//    · Reflection angle depends on hit position along paddle (edges = steep)
//    · Level 3+: top row bricks require 2 hits (displayed solid)
//    · Ball speed multiplied by 1.18× per level
//    · Score = (BK_R - row) × 5  (higher rows = more points)
// ═════════════════════════════════════════════════════════════════════════════
#define BK_C    8     // brick columns
#define BK_R    4     // brick rows
#define BK_BW  13     // brick width (px)
#define BK_BH   4     // brick height (px)
#define BK_BG   2     // gap between bricks (px)
#define BK_PW  24     // paddle width (px)
#define BK_PH   3     // paddle height (px)
#define BK_PY  (SCR_H - 5)   // paddle Y position (5 px from bottom)
#define BK_PS   3     // paddle speed (px per frame)
#define BK_BR   2     // ball radius (px)

static bool    bkG[BK_R][BK_C];      // brick alive flags
static uint8_t bkHits[BK_R][BK_C];   // remaining hits per brick
static int     bkLeft;               // bricks remaining
static float   bkBX, bkBY;          // ball position
static float   bkBDX, bkBDY;        // ball velocity
static float   bkPX;                 // paddle left edge X
static uint32_t bkLast, bkHold;
static uint8_t  bkLvl;              // current level (starts at 1)
static bool     bkLaunch;           // ball launched from paddle?

void bkReset() {
  bkLeft = 0;
  for (int r = 0; r < BK_R; r++) for (int c = 0; c < BK_C; c++) {
    bkG[r][c] = true;
    bkHits[r][c] = (bkLvl >= 3 && r == 0) ? 2 : 1;   // top row armoured at level 3+
    bkLeft++;
  }
}

void breakoutInit() {
  bkPX = (SCR_W - BK_PW) / 2.0f;
  bkLvl = 1; curScore = 0; bkLaunch = false; bkHold = 0;
  bkLast = millis(); bkReset();
  bkBX = bkPX + BK_PW / 2.0f; bkBY = BK_PY - BK_BR - 1;
  bkBDX = 1.8f; bkBDY = -2.2f;
}

void breakoutUpdate() {
  // Hold A → menu
  if (B.hA) { if (!bkHold) bkHold = millis(); if (millis() - bkHold > 700) { gState = ST_MENU; return; } }
  else bkHold = 0;

  uint32_t now = millis();
  if (now - bkLast < 28) return;   // ~35 FPS physics
  bkLast = now;

  // Move paddle (clamped to screen)
  if (B.hL) bkPX -= BK_PS;
  if (B.hR) bkPX += BK_PS;
  bkPX = constrain(bkPX, 0.0f, (float)(SCR_W - BK_PW));

  // Pre-launch: ball tracks paddle centre
  if (!bkLaunch) {
    bkBX = bkPX + BK_PW / 2.0f;
    if (B.pA) { bkLaunch = true; sfxBounce(); }
    return;
  }

  // Move ball
  bkBX += bkBDX; bkBY += bkBDY;

  // Wall bounces
  if (bkBX - BK_BR <= 0)     { bkBDX =  fabsf(bkBDX); sfxBounce(); }
  if (bkBX + BK_BR >= SCR_W) { bkBDX = -fabsf(bkBDX); sfxBounce(); }
  if (bkBY - BK_BR <= 10)    { bkBDY =  fabsf(bkBDY); sfxBounce(); }

  // Ball lost (fell off bottom)
  if (bkBY > SCR_H + 4) {
    sfxDie(); gameOver = true; goTimer = millis(); saveHiScore();
  }

  // Paddle collision
  if (bkBY + BK_BR >= BK_PY && bkBY + BK_BR <= BK_PY + BK_PH + 2 &&
      bkBX + BK_BR >= bkPX  && bkBX - BK_BR <= bkPX + BK_PW) {
    bkBDY = -fabsf(bkBDY);
    float rel = (bkBX - bkPX) / BK_PW - 0.5f;  // –0.5..+0.5
    bkBDX += rel * 2.2f;
    bkBDX = constrain(bkBDX, -5.0f, 5.0f);
    sfxBounce();
  }

  // Brick collisions (check each row, break on first hit per frame)
  for (int r = 0; r < BK_R; r++) {
    bool hit = false;
    for (int c = 0; c < BK_C; c++) {
      if (!bkG[r][c]) continue;
      float bx = c * (BK_BW + BK_BG) + 1.0f;
      float by = 11.0f + r * (BK_BH + 2);
      if (bkBX + BK_BR > bx && bkBX - BK_BR < bx + BK_BW &&
          bkBY + BK_BR > by && bkBY - BK_BR < by + BK_BH) {
        bkHits[r][c]--;
        if (bkHits[r][c] <= 0) { bkG[r][c] = false; bkLeft--; }
        // Resolve bounce axis (whichever overlap is smaller)
        float ox = min(bkBX + BK_BR, bx + BK_BW) - max(bkBX - BK_BR, bx);
        float oy = min(bkBY + BK_BR, by + BK_BH) - max(bkBY - BK_BR, by);
        if (ox < oy) bkBDX = -bkBDX; else bkBDY = -bkBDY;
        curScore += (BK_R - r) * 5;   // higher row = more points
        sfxBrick(); hit = true;

        // Level cleared!
        if (bkLeft == 0) {
          sfxWin(); bkLvl++;
          float spd = 1.0f + (bkLvl - 1) * 0.18f;
          bkBDX = (bkBDX > 0 ? 1.8f : -1.8f) * spd;
          bkBDY = -2.2f * spd;
          bkPX  = (SCR_W - BK_PW) / 2.0f;
          bkBX  = bkPX + BK_PW / 2.0f;
          bkBY  = BK_PY - BK_BR - 1;
          bkLaunch = false; bkReset();
        }
        break;
      }
    }
    if (hit) break;
  }
}

void breakoutDraw() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0,   0); display.print(F("BREAKOUT  Sc:")); display.print(curScore);
  display.setCursor(104, 0); display.print(F("L:")); display.print(bkLvl);
  display.drawFastHLine(0, 9, SCR_W, SSD1306_WHITE);

  for (int r = 0; r < BK_R; r++) for (int c = 0; c < BK_C; c++) {
    if (!bkG[r][c]) continue;
    int px = c * (BK_BW + BK_BG) + 1, py = 11 + r * (BK_BH + 2);
    // Even rows and armoured bricks: solid fill. Odd rows: outlined with centre line
    if (r % 2 == 0 || bkHits[r][c] > 1) {
      display.fillRect(px, py, BK_BW, BK_BH, SSD1306_WHITE);
    } else {
      display.drawRect(px, py, BK_BW, BK_BH, SSD1306_WHITE);
      display.drawFastHLine(px + 1, py + 2, BK_BW - 2, SSD1306_WHITE);
    }
  }

  display.fillRect((int)bkPX, BK_PY, BK_PW, BK_PH, SSD1306_WHITE);
  display.fillCircle((int)bkBX, (int)bkBY, BK_BR, SSD1306_WHITE);
  if (!bkLaunch) { display.setCursor(26, 38); display.print(F("[A] Launch!")); }
  if (gameOver) drawGameOver();
  display.display();
}

// ─────────────────────────────────────────────────────────────────────────────
//  GAME DISPATCHER  —  routes init/update/draw to the correct game
// ─────────────────────────────────────────────────────────────────────────────
void gameInit(uint8_t g) {
  activeGame = g; gameOver = false; curScore = 0;
  switch (g) {
    case 0: snakeInit();    break;
    case 1: pongInit();     break;
    case 2: tttInit();      break;
    case 3: trexInit();     break;
    case 4: mazeInit();     break;
    case 5: breakoutInit(); break;
  }
}

void gameUpdate() {
  if (gameOver) {
    if (curScore > hiScores[activeGame]) hiScores[activeGame] = curScore;
    if (B.pA)         { gameInit(activeGame); return; }
    if (B.pL || B.pR) { gState = ST_MENU; gameOver = false; return; }
    return;
  }
  switch (activeGame) {
    case 0: snakeUpdate();    break;
    case 1: pongUpdate();     break;
    case 2: tttUpdate();      break;
    case 3: trexUpdate();     break;
    case 4: mazeUpdate();     break;
    case 5: breakoutUpdate(); break;
  }
}

void gameDraw() {
  switch (activeGame) {
    case 0: snakeDraw();    break;
    case 1: pongDraw();     break;
    case 2: tttDraw();      break;
    case 3: trexDraw();     break;
    case 4: mazeDraw();     break;
    case 5: breakoutDraw(); break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
//
//  Runs once at power-on:
//    1. Configure button pins as INPUT_PULLDOWN (HIGH when pressed)
//    2. Initialize I2C and OLED display
//    3. Load hi-scores from NVS flash
//    4. Seed random number generator from hardware RNG
//    5. Initialize star field and state machine
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // ⚠ INPUT_PULLDOWN: external 10kΩ to GND, button goes to 3.3V
  //   Unpressed = LOW (0),  Pressed = HIGH (1)
  pinMode(BTN_L, INPUT_PULLDOWN);
  pinMode(BTN_R, INPUT_PULLDOWN);
  pinMode(BTN_A, INPUT_PULLDOWN);
  pinMode(PIN_BUZ, OUTPUT);
  digitalWrite(PIN_BUZ, LOW);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED fail — check wiring & I2C address"));
    while (true) { tone(PIN_BUZ, 220, 500); delay(900); }   // error tone loop
  }

  // Load persistent hi-scores from NVS flash (Preferences library)
  prefs.begin("scores", false);
  for (int i = 0; i < NUM_GAMES; i++)
    hiScores[i] = prefs.getUInt(("g" + String(i)).c_str(), 0);

  randomSeed(esp_random());          // hardware random seed on ESP32
  memset(&B, 0, sizeof(B));          // clear button state
  initStars();

  gState = ST_BOOT; bootPhase = 0; flagY = SCR_H + 4;
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAIN LOOP  (capped at ~60 FPS)
//
//  Every iteration:
//    · sTick() — advance the non-blocking sound queue
//    · If 16 ms have elapsed: read buttons and update+draw the current state
//
//  The 16 ms gate ensures consistent physics regardless of draw time,
//  while sTick() still fires on every iteration for smooth audio.
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t loopT = 0;

void loop() {
  uint32_t now = millis();
  if (now - loopT < 16) { sTick(); return; }  // yield if too soon
  loopT = now;

  readBtn();
  sTick();

  switch (gState) {
    case ST_BOOT:       bootUpdate();              break;
    case ST_TYPEWRITER: typewriterUpdate();        break;
    case ST_MENU:       menuDraw(); menuUpdate();  break;
    case ST_GAME:       gameUpdate(); gameDraw();  break;
  }
}
