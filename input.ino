const int J1_UP = 2;
const int J1_DOWN = 3;
const int J1_LEFT = 4;
const int J1_RIGHT = 5;

const int J2_UP = 6;
const int J2_DOWN = 7;
const int J2_LEFT = 8;
const int J2_RIGHT = 9;

const int OUT_PIN = 10;

const unsigned int START_PULSE_US = 2000;
const unsigned int BIT_HIGH_US    = 1000;
const unsigned int BIT_LOW_US     = 400;
const unsigned int FRAME_GAP_US   = 3000;

uint8_t readJoystickBits() {
  uint8_t state = 0;

  if (digitalRead(J1_UP)    == LOW) state |= (1 << 0);
  if (digitalRead(J1_DOWN)  == LOW) state |= (1 << 1);
  if (digitalRead(J1_LEFT)  == LOW) state |= (1 << 2);
  if (digitalRead(J1_RIGHT) == LOW) state |= (1 << 3);

  if (digitalRead(J2_UP)    == LOW) state |= (1 << 4);
  if (digitalRead(J2_DOWN)  == LOW) state |= (1 << 5);
  if (digitalRead(J2_LEFT)  == LOW) state |= (1 << 6);
  if (digitalRead(J2_RIGHT) == LOW) state |= (1 << 7);

  return state;
}

void sendBit(bool bitVal) {
  digitalWrite(OUT_PIN, HIGH);
  delayMicroseconds(bitVal ? BIT_HIGH_US : BIT_LOW_US);
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(BIT_LOW_US);
}

void sendFrame(uint8_t value) {
  digitalWrite(OUT_PIN, HIGH);
  delayMicroseconds(START_PULSE_US);
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(BIT_LOW_US);

  for (int i = 0; i < 8; i++) {
    sendBit((value >> i) & 0x01);
  }

  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(FRAME_GAP_US);
}

void setup() {
  pinMode(J1_UP, INPUT_PULLUP);
  pinMode(J1_DOWN, INPUT_PULLUP);
  pinMode(J1_LEFT, INPUT_PULLUP);
  pinMode(J1_RIGHT, INPUT_PULLUP);

  pinMode(J2_UP, INPUT_PULLUP);
  pinMode(J2_DOWN, INPUT_PULLUP);
  pinMode(J2_LEFT, INPUT_PULLUP);
  pinMode(J2_RIGHT, INPUT_PULLUP);

  pinMode(OUT_PIN, OUTPUT);
  digitalWrite(OUT_PIN, LOW);

  Serial.begin(115200);
}

void loop() {
  uint8_t state = readJoystickBits();

  Serial.print("STATE=");
  Serial.println(state, BIN);

  sendFrame(state);

  delay(20);
}
