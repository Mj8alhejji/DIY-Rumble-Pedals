/*
 * Rumble Pedals — DUMB EXECUTOR — ESP32-S3, pins 4,7,17,3
 * All limits/logic live in SimHub. This just receives 4 values and drives motors.
 * Slots: brakeHigh, brakeLow, throttleHigh, throttleLow
 * SimHub sends: 4 values 0-100, comma separated, newline.
 
 * Serial: 115200, Newline.  Menu: 1=assign  2=test  3=show  reset
 */

#include <Preferences.h>

#define NUM_MOTORS 4
uint8_t motorPins[NUM_MOTORS] = { 4, 7, 17, 3 };
#define PWM_FREQ 20000

const char* slotNames[NUM_MOTORS] = {
  "Brake HIGH", "Brake LOW", "Throttle HIGH", "Throttle LOW"
};
int slotToMotor[NUM_MOTORS] = { 0, 1, 2, 3 };
Preferences prefs;

bool assigning = false;
int  assignMotor = 0;
int  motorToSlot[NUM_MOTORS] = { -1,-1,-1,-1 };
bool slotTaken[NUM_MOTORS] = { false,false,false,false };
bool awaitingReset = false;

String inputBuffer = "";
unsigned long lastCmd = 0;
const unsigned long TIMEOUT_MS = 500;

void setup() {
  Serial.begin(115200);
  loadAssign();
  for (int i = 0; i < NUM_MOTORS; i++) {
    ledcAttach(motorPins[i], PWM_FREQ, 8);
    ledcWrite(motorPins[i], 0);
  }
  delay(500);
  printMenu();
}

void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      String line = inputBuffer; inputBuffer = "";
      if (assigning) handleAssign(line);
      else           handleLine(line);
    } else if (c != '\r') inputBuffer += c;
  }
  if (!assigning && lastCmd != 0 && millis() - lastCmd > TIMEOUT_MS) {
    for (int i = 0; i < NUM_MOTORS; i++) ledcWrite(motorPins[i], 0);
    lastCmd = 0;
  }
}

void loadAssign() {
  prefs.begin("rumble4", true);
  for (int i = 0; i < NUM_MOTORS; i++)
    slotToMotor[i] = prefs.getInt(("slot" + String(i)).c_str(), slotToMotor[i]);
  prefs.end();
}
void saveAssign() {
  prefs.begin("rumble4", false);
  for (int i = 0; i < NUM_MOTORS; i++)
    prefs.putInt(("slot" + String(i)).c_str(), slotToMotor[i]);
  prefs.end();
  Serial.println("Saved.");
}
void resetAssign() {
  for (int i = 0; i < NUM_MOTORS; i++) slotToMotor[i] = i;
  prefs.begin("rumble4", false); prefs.clear(); prefs.end();
  for (int i = 0; i < NUM_MOTORS; i++) ledcWrite(motorPins[i], 0);
  Serial.println("Reset.");
}

void handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  if (awaitingReset) {
    awaitingReset = false;
    if (line.equalsIgnoreCase("yes")) resetAssign(); else Serial.println("Cancelled.");
    printMenu(); return;
  }
  if (line == "1") { startAssign(); return; }
  if (line == "2") { testRun(); return; }
  if (line == "3" || line.equalsIgnoreCase("show")) { showAssign(); return; }
  if (line.equalsIgnoreCase("reset")) { Serial.println("Reset? type 'yes'."); awaitingReset = true; return; }
  if (line.equalsIgnoreCase("menu")) { printMenu(); return; }
  applyValues(line);
}

// receives 4 values 0-100, drives motors DIRECTLY (no limits applied here)
void applyValues(String line) {
  int v[NUM_MOTORS]; int idx = 0, start = 0;
  for (int i = 0; i <= (int)line.length() && idx < NUM_MOTORS; i++) {
    if (i == (int)line.length() || line.charAt(i) == ',') {
      v[idx++] = constrain(line.substring(start, i).toInt(), 0, 100);
      start = i + 1;
    }
  }
  if (idx == NUM_MOTORS) {
    for (int slot = 0; slot < NUM_MOTORS; slot++) {
      int m = slotToMotor[slot];
      ledcWrite(motorPins[m], map(v[slot], 0, 100, 0, 255));   // straight 0-100 -> 0-255
    }
    lastCmd = millis();
  }
}

void testRun() {
  Serial.println("=== TEST: ramp each motor ===");
  for (int slot = 0; slot < NUM_MOTORS; slot++) {
    int m = slotToMotor[slot];
    Serial.print(slotNames[slot]); Serial.print(" (motor "); Serial.print(m + 1); Serial.println(")");
    for (int p = 0; p <= 255; p++) { ledcWrite(motorPins[m], p); delay(10); }
    for (int p = 255; p >= 0; p--) { ledcWrite(motorPins[m], p); delay(5); }
    ledcWrite(motorPins[m], 0); delay(400);
  }
  Serial.println("Done."); printMenu();
}

void startAssign() {
  assigning = true; assignMotor = 0;
  for (int i = 0; i < NUM_MOTORS; i++) { motorToSlot[i] = -1; slotTaken[i] = false; ledcWrite(motorPins[i], 0); }
  Serial.println("=== ASSIGN ==="); buzzCurrentMotor();
}
void buzzCurrentMotor() {
  for (int i = 0; i < NUM_MOTORS; i++) ledcWrite(motorPins[i], 0);
  ledcWrite(motorPins[assignMotor], 200);
  Serial.print(">>> Motor "); Serial.print(assignMotor + 1); Serial.println(" buzzing. What is it?");
  for (int s = 0; s < NUM_MOTORS; s++)
    if (!slotTaken[s]) { Serial.print("  "); Serial.print(s + 1); Serial.print(" = "); Serial.println(slotNames[s]); }
  Serial.println("  b = back");
}
void handleAssign(String line) {
  line.trim();
  if (line.equalsIgnoreCase("b")) {
    if (assignMotor > 0) {
      assignMotor--;
      int ps = motorToSlot[assignMotor];
      if (ps >= 0) slotTaken[ps] = false;
      motorToSlot[assignMotor] = -1; buzzCurrentMotor();
    } else { for (int i=0;i<NUM_MOTORS;i++) ledcWrite(motorPins[i],0); assigning=false; printMenu(); }
    return;
  }
  int s = line.toInt();
  if (s < 1 || s > NUM_MOTORS) { Serial.println("Pick from list, or b."); return; }
  if (slotTaken[s - 1]) { Serial.println("Taken."); return; }
  motorToSlot[assignMotor] = s - 1; slotTaken[s - 1] = true;
  ledcWrite(motorPins[assignMotor], 0);
  Serial.print("Motor "); Serial.print(assignMotor + 1); Serial.print(" = "); Serial.println(slotNames[s - 1]);
  assignMotor++;
  if (assignMotor < NUM_MOTORS) buzzCurrentMotor();
  else {
    for (int m = 0; m < NUM_MOTORS; m++) slotToMotor[motorToSlot[m]] = m;
    assigning = false; Serial.println("=== DONE ==="); saveAssign(); showAssign(); printMenu();
  }
}

void printMenu() {
  Serial.println();
  Serial.println("=== RUMBLE PEDALS (executor) ===");
  Serial.println("  1 = assign   2 = test   3 = show   reset");
  Serial.println("All effects/limits are set in SimHub.");
  showAssign();
}
void showAssign() {
  for (int slot = 0; slot < NUM_MOTORS; slot++) {
    Serial.print("  "); Serial.print(slotNames[slot]);
    Serial.print(" -> motor "); Serial.println(slotToMotor[slot] + 1);
  }
}