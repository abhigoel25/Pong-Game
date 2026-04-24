#include <Wire.h>
#include <SPI.h>
#include <PCA9557.h>
#include "gfx_conf.h"

PCA9557 Out;

#define W 800
#define H 480
#define PADDLE_W 15
#define PADDLE_H 100
#define BALL_SIZE 14

#define TOP_BOUNDARY 50

#define TARGET_FPS 90
#define FRAME_TIME (1000 / TARGET_FPS)
#define WIN_SCORE 10

#define PADDLE_SPEED 7

#define BASE_SPEED_X 3.0f
#define BASE_SPEED_Y 3.5f
#define MIN_DY 1.5f
#define MAX_SPEED_MULT 3.0f
#define SPEED_INCREMENT 0.15f

// ---------- Sleep Mode Variables ----------
#define SLEEP_TIMEOUT 15000 // 15 seconds in milliseconds
unsigned long lastInputTime = 0;
bool isSleeping = false;

// ---------- One-wire joystick input on GPIO_D / IO38 ----------
#define JOY_RX_PIN 38

volatile uint32_t riseTimeUs = 0;
volatile bool inFrame = false;
volatile uint8_t rxBitIndex = 0;
volatile uint8_t rxWorkingByte = 0;
volatile uint8_t latestJoyState = 0;
volatile bool newJoyPacket = false;

#define START_MIN_US 1500
#define BIT_ONE_MIN_US 700

uint8_t joyState = 0;

// ---------- Game state ----------
float ballX = W / 2, ballY = H / 2;
float ballDX = BASE_SPEED_X, ballDY = BASE_SPEED_Y;
float currentSpeedMult = 1.0f;
int hitCount = 0;

int p1Y = H / 2 - 50;
int p2Y = H / 2 - 50;
int score1 = 0, score2 = 0;
bool gameOver = false;
int winner = 0;
unsigned long lastFrame = 0;

// ---------- Function declarations ----------
void drawScores();
void drawGameOver();
void drawCenterLine();
void restoreCenterLine(int y1, int y2);
void resetBall(int direction);

// ---------- Interrupt receiver ----------
void IRAM_ATTR handleJoyEdge() {
int level = digitalRead(JOY_RX_PIN);
uint32_t nowUs = micros();

if (level == HIGH) {
riseTimeUs = nowUs;
} else {
uint32_t highDur = nowUs - riseTimeUs;

if (highDur >= START_MIN_US) {
inFrame = true;
rxBitIndex = 0;
rxWorkingByte = 0;
return;
}

if (inFrame) {
bool bitVal = (highDur >= BIT_ONE_MIN_US);
if (bitVal) rxWorkingByte |= (1 << rxBitIndex);
rxBitIndex++;

if (rxBitIndex >= 8) {
latestJoyState = rxWorkingByte;
newJoyPacket = true;
inFrame = false;
}
}
}
}

void setup() {
Serial.begin(115200);

#if defined(CrowPanel_50) || defined(CrowPanel_70)
Wire.begin(19, 20);
Out.reset();
Out.setMode(IO_OUTPUT);
Out.setState(IO0, IO_LOW);
Out.setState(IO1, IO_LOW);
delay(20);
Out.setState(IO0, IO_HIGH); // Backlight ON
delay(100);
Out.setMode(IO1, IO_INPUT);
#endif

tft.begin();
tft.fillScreen(TFT_BLACK);
tft.setTextSize(3);

tft.drawFastHLine(0, TOP_BOUNDARY - 2, W, TFT_DARKGREY);

drawCenterLine();
drawScores();

pinMode(JOY_RX_PIN, INPUT);
attachInterrupt(digitalPinToInterrupt(JOY_RX_PIN), handleJoyEdge, CHANGE);

resetBall(1);

lastInputTime = millis(); // Initialize the sleep timer
Serial.println("Pong Starting with IO38 joystick input");
}

void loop() {
unsigned long now = millis();
if (now - lastFrame < FRAME_TIME) return;
lastFrame = now;

// --- Fetch Joystick Data ---
if (newJoyPacket) {
noInterrupts();
joyState = latestJoyState;
newJoyPacket = false;
interrupts();
}

// --- SLEEP MODE LOGIC ---

// 1. If any joystick is being pushed, reset the sleep timer
if (joyState != 0) {
lastInputTime = now;
}

// 2. If we are awake but 15 seconds have passed with no input, go to sleep
if (!isSleeping && (now - lastInputTime > SLEEP_TIMEOUT)) {
isSleeping = true;
Serial.println("entering sleep mode");

// Tell the players it's going to sleep visually
tft.fillScreen(TFT_BLACK);
tft.setTextColor(TFT_WHITE, TFT_BLACK);
tft.setTextSize(3);
tft.setCursor(W / 2 - 170, H / 2);
tft.print("ENTERING SLEEP MODE");
delay(1000); // Give them 1 second to read it

// Now completely blackout the screen and kill the backlight
tft.fillScreen(TFT_BLACK);
#if defined(CrowPanel_50) || defined(CrowPanel_70)
Out.setState(IO0, IO_LOW);
#endif
}

// 3. Handle wake-up and freeze the game while asleep
if (isSleeping) {
// Wait for ANY joystick input to wake up
if (joyState != 0) {
isSleeping = false;
Serial.println("waking up - fresh game");

// Wake up the hardware backlight
#if defined(CrowPanel_50) || defined(CrowPanel_70)
Out.setState(IO0, IO_HIGH);
delay(100);
Out.setMode(IO1, IO_INPUT);
#endif

// Start a fresh game instantly
score1 = 0;
score2 = 0;
gameOver = false;
p1Y = H / 2 - 50;
p2Y = H / 2 - 50;

tft.fillScreen(TFT_BLACK);
tft.drawFastHLine(0, TOP_BOUNDARY - 2, W, TFT_DARKGREY);
drawCenterLine();
drawScores();
resetBall(1);

// Reset the timer so it doesn't instantly fall asleep again!
lastInputTime = millis();
}
return; // SKIP the rest of the loop so the game doesn't play in the background!
}

// --- Normal Game Loop (Only runs if awake) ---

if (gameOver) {
drawGameOver();
delay(5000);
score1 = 0;
score2 = 0;
gameOver = false;
p1Y = H / 2 - 50;
p2Y = H / 2 - 50;
tft.fillScreen(TFT_BLACK);
tft.drawFastHLine(0, TOP_BOUNDARY - 2, W, TFT_DARKGREY);
drawCenterLine();
resetBall(1);
drawScores();
lastInputTime = millis(); // Don't sleep immediately after a game ends
return;
}

int old_p1Y = p1Y;
int old_p2Y = p2Y;
int oldBallX = (int)ballX;
int oldBallY = (int)ballY;

bool j1Up = joyState & (1 << 0);
bool j1Down = joyState & (1 << 1);
bool j1Left = joyState & (1 << 2);
bool j1Right = joyState & (1 << 3);

bool j2Up = joyState & (1 << 4);
bool j2Down = joyState & (1 << 5);
bool j2Left = joyState & (1 << 6);
bool j2Right = joyState & (1 << 7);

// --- Player paddle controls ---
if (j1Up && !j1Down) p1Y -= PADDLE_SPEED;
if (j1Down && !j1Up) p1Y += PADDLE_SPEED;

if (j2Up && !j2Down) p2Y -= PADDLE_SPEED;
if (j2Down && !j2Up) p2Y += PADDLE_SPEED;

p1Y = constrain(p1Y, TOP_BOUNDARY, H - PADDLE_H);
p2Y = constrain(p2Y, TOP_BOUNDARY, H - PADDLE_H);

// --- Optional reset shortcut ---
if (j1Left && j1Right && j2Left && j2Right) {
score1 = 0;
score2 = 0;
p1Y = H / 2 - 50;
p2Y = H / 2 - 50;
tft.fillScreen(TFT_BLACK);
tft.drawFastHLine(0, TOP_BOUNDARY - 2, W, TFT_DARKGREY);
drawCenterLine();
drawScores();
resetBall(1);
delay(300);
return;
}

// --- Ball Physics ---
ballX += ballDX;
ballY += ballDY;

if (ballY <= TOP_BOUNDARY) {
ballY = TOP_BOUNDARY;
ballDY = abs(ballDY);
}
if (ballY >= H - BALL_SIZE) {
ballY = H - BALL_SIZE;
ballDY = -abs(ballDY);
}

if (ballX <= PADDLE_W + 2 && ballX >= 0 && (int)ballY + BALL_SIZE > p1Y && (int)ballY < p1Y + PADDLE_H) {
ballX = PADDLE_W + 3;
if (currentSpeedMult < MAX_SPEED_MULT) currentSpeedMult += SPEED_INCREMENT;
hitCount++;
ballDX = BASE_SPEED_X * currentSpeedMult;
float hitPos = ((ballY + BALL_SIZE / 2) - (p1Y + PADDLE_H / 2)) / (float)(PADDLE_H / 2);
ballDY = hitPos * 4.0f * currentSpeedMult;
if (ballDY >= 0 && ballDY < MIN_DY) ballDY = MIN_DY;
if (ballDY < 0 && ballDY > -MIN_DY) ballDY = -MIN_DY;
}

if (ballX >= W - PADDLE_W - BALL_SIZE - 2 && ballX <= W && (int)ballY + BALL_SIZE > p2Y && (int)ballY < p2Y + PADDLE_H) {
ballX = W - PADDLE_W - BALL_SIZE - 3;
if (currentSpeedMult < MAX_SPEED_MULT) currentSpeedMult += SPEED_INCREMENT;
hitCount++;
ballDX = -BASE_SPEED_X * currentSpeedMult;
float hitPos = ((ballY + BALL_SIZE / 2) - (p2Y + PADDLE_H / 2)) / (float)(PADDLE_H / 2);
ballDY = hitPos * 4.0f * currentSpeedMult;
if (ballDY >= 0 && ballDY < MIN_DY) ballDY = MIN_DY;
if (ballDY < 0 && ballDY > -MIN_DY) ballDY = -MIN_DY;
}

// --- Scoring ---
if (ballX < 0) {
score2++;
tft.fillScreen(TFT_BLACK);
tft.drawFastHLine(0, TOP_BOUNDARY - 2, W, TFT_DARKGREY);
drawCenterLine();
drawScores();
if (score2 >= WIN_SCORE) { gameOver = true; winner = 2; return; }
resetBall(1);
delay(800);
return;
}

if (ballX > W) {
score1++;
tft.fillScreen(TFT_BLACK);
tft.drawFastHLine(0, TOP_BOUNDARY - 2, W, TFT_DARKGREY);
drawCenterLine();
drawScores();
if (score1 >= WIN_SCORE) { gameOver = true; winner = 1; return; }
resetBall(-1);
delay(800);
return;
}

// --- Rendering ---
if (p1Y > old_p1Y) tft.fillRect(0, old_p1Y, PADDLE_W, p1Y - old_p1Y, TFT_BLACK);
else if (p1Y < old_p1Y) tft.fillRect(0, p1Y + PADDLE_H, PADDLE_W, old_p1Y - p1Y, TFT_BLACK);
tft.fillRect(0, p1Y, PADDLE_W, PADDLE_H, TFT_WHITE);

if (p2Y > old_p2Y) tft.fillRect(W - PADDLE_W, old_p2Y, PADDLE_W, p2Y - old_p2Y, TFT_BLACK);
else if (p2Y < old_p2Y) tft.fillRect(W - PADDLE_W, p2Y + PADDLE_H, PADDLE_W, old_p2Y - p2Y, TFT_BLACK);
tft.fillRect(W - PADDLE_W, p2Y, PADDLE_W, PADDLE_H, TFT_WHITE);

tft.fillRect((int)ballX, (int)ballY, BALL_SIZE, BALL_SIZE, TFT_YELLOW);

int bdx = (int)ballX - oldBallX;
int bdy = (int)ballY - oldBallY;

if (abs(bdx) >= BALL_SIZE || abs(bdy) >= BALL_SIZE) {
tft.fillRect(oldBallX, oldBallY, BALL_SIZE, BALL_SIZE, TFT_BLACK);
} else {
if (bdx > 0) tft.fillRect(oldBallX, oldBallY, bdx, BALL_SIZE, TFT_BLACK);
else if (bdx < 0) tft.fillRect(oldBallX + BALL_SIZE + bdx, oldBallY, -bdx, BALL_SIZE, TFT_BLACK);

if (bdy > 0) tft.fillRect(oldBallX, oldBallY, BALL_SIZE, bdy, TFT_BLACK);
else if (bdy < 0) tft.fillRect(oldBallX, oldBallY + BALL_SIZE + bdy, BALL_SIZE, -bdy, TFT_BLACK);
}

if (oldBallX <= W / 2 + 2 && oldBallX + BALL_SIZE >= W / 2 - 2) {
restoreCenterLine(oldBallY, oldBallY + BALL_SIZE);
}
}

void drawCenterLine() {
for (int y = TOP_BOUNDARY; y < H; y += 40) {
tft.fillRect(W / 2 - 2, y, 4, 20, TFT_DARKGREY);
}
}

void restoreCenterLine(int y1, int y2) {
for (int y = TOP_BOUNDARY; y < H; y += 40) {
if (y + 20 >= y1 && y <= y2) {
tft.fillRect(W / 2 - 2, y, 4, 20, TFT_DARKGREY);
}
}
}

void resetBall(int direction) {
hitCount = 0;
currentSpeedMult = 1.0f;
ballX = W / 2;
ballY = H / 2;
ballDX = direction * BASE_SPEED_X;
ballDY = (random(2) == 0) ? BASE_SPEED_Y : -BASE_SPEED_Y;
}

void drawScores() {
tft.setTextColor(TFT_WHITE, TFT_BLACK);
tft.setTextSize(4);

int charW = 6 * 4;
String s1 = String(score1);
String s2 = String(score2);

int x1 = W / 2 - 20 - (s1.length() * charW);
int x2 = W / 2 + 20;

tft.fillRect(x1 - 2, 0, s1.length() * charW + 4, TOP_BOUNDARY - 2, TFT_BLACK);
tft.fillRect(x2 - 2, 0, s2.length() * charW + 4, TOP_BOUNDARY - 2, TFT_BLACK);

tft.setCursor(x1, 10);
tft.print(score1);
tft.setCursor(x2, 10);
tft.print(score2);
}

void drawGameOver() {
tft.fillScreen(TFT_BLACK);
tft.setTextColor(TFT_YELLOW, TFT_BLACK);
tft.setTextSize(5);
tft.setCursor(W / 2 - 200, H / 2 - 60);
tft.print("PLAYER ");
tft.print(winner);
tft.setCursor(W / 2 - 130, H / 2 + 20);
tft.print("WINS!");
tft.setTextSize(3);
tft.setTextColor(TFT_WHITE, TFT_BLACK);
tft.setCursor(W / 2 - 180, H / 2 + 100);
tft.print("Restarting in 5s...");
}