
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <string.h>

// LCD and servo
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo gateServo;

// Existing connections
constexpr byte PROXIMITY_PIN = 7;
constexpr byte SERVO_PIN     = 8;
constexpr byte BUZZER_PIN    = 5;
constexpr byte FAN_PIN       = 6;
constexpr byte ALCOHOL_PIN   = A0;

// Adjust after testing the MQ-3 sensor
constexpr int ALCOHOL_THRESHOLD = 350;

// Servo positions
constexpr int GATE_CLOSED_ANGLE = 0;
constexpr int GATE_OPEN_ANGLE   = 90;

// Most proximity modules output LOW when detecting
constexpr byte DETECTED_LEVEL = LOW;

// Timing
constexpr unsigned long WARMUP_TIME_MS = 30000UL;
constexpr unsigned long DETECT_TIME_MS = 1500UL;
constexpr unsigned long CHECK_TIME_MS  = 5000UL;

enum SystemState {
  STARTUP,
  WAITING,
  CHECKING,
  ALCOHOL_BLOCKED,
  GATE_OPEN
};

SystemState state = STARTUP;

unsigned long startupTime = 0;
unsigned long detectionStartTime = 0;
unsigned long checkStartTime = 0;

bool detectionTiming = false;

// LCD text cache
char lastLine1[17] = "";
char lastLine2[17] = "";

// Update LCD only when the text changes
void showMessage(const char* line1, const char* line2) {
  if (strcmp(lastLine1, line1) == 0 &&
      strcmp(lastLine2, line2) == 0) {
    return;
  }

  strncpy(lastLine1, line1, 16);
  lastLine1[16] = '\0';

  strncpy(lastLine2, line2, 16);
  lastLine2[16] = '\0';

  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(lastLine1);

  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print(lastLine2);
}

// Raw proximity reading
bool rawVehicleDetected() {
  return digitalRead(PROXIMITY_PIN) == DETECTED_LEVEL;
}

// Confirm arrival only after continuous detection
bool vehicleDetected() {
  if (!rawVehicleDetected()) {
    detectionTiming = false;
    detectionStartTime = 0;
    return false;
  }

  if (!detectionTiming) {
    detectionTiming = true;
    detectionStartTime = millis();
  }

  return millis() - detectionStartTime >= DETECT_TIME_MS;
}

// Average MQ-3 readings
int readAlcoholSensor() {
  long total = 0;

  for (byte i = 0; i < 10; i++) {
    total += analogRead(ALCOHOL_PIN);
    delay(10);
  }

  return total / 10;
}

void setFan(bool on) {
  digitalWrite(FAN_PIN, on ? HIGH : LOW);
}

void setBuzzer(bool on) {
  if (on) {
    tone(BUZZER_PIN, 2000);
  } else {
    noTone(BUZZER_PIN);
  }
}

void closeGate() {
  gateServo.write(GATE_CLOSED_ANGLE);
}

void resetToWaiting() {
  setFan(false);
  setBuzzer(false);
  closeGate();

  detectionTiming = false;
  detectionStartTime = 0;

  state = WAITING;
  showMessage("System Ready", "Waiting vehicle");
}

void setup() {
  Serial.begin(9600);

  pinMode(PROXIMITY_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  setFan(false);
  setBuzzer(false);

  gateServo.attach(SERVO_PIN);
  closeGate();

  lcd.init();
  lcd.backlight();

  showMessage("ALCOHOL", "DETECTION SYSTEM");
  delay(2000);

  showMessage("Sensor warming", "Please wait...");
  startupTime = millis();
}

void loop() {
  // Initial MQ-3 warm-up
  if (state == STARTUP) {
    setFan(false);
    setBuzzer(false);

    if (millis() - startupTime >= WARMUP_TIME_MS) {
      resetToWaiting();
    }
    return;
  }

  // WAITING: filter brief detections on arrival
  if (state == WAITING) {
    setFan(false);
    setBuzzer(false);
    closeGate();

    if (vehicleDetected()) {
      checkStartTime = millis();

      setFan(true);
      showMessage("Vehicle detected", "Checking alcohol");

      state = CHECKING;
    }
  }

  // CHECKING: fan runs only during the check
  else if (state == CHECKING) {
    setFan(true);
    closeGate();

    // Cancel if vehicle leaves before checking finishes
    if (!rawVehicleDetected()) {
      resetToWaiting();
      return;
    }

    if (millis() - checkStartTime >= CHECK_TIME_MS) {
      int alcoholValue = readAlcoholSensor();

      setFan(false);

      Serial.print("MQ-3 reading: ");
      Serial.println(alcoholValue);

      if (alcoholValue >= ALCOHOL_THRESHOLD) {
        closeGate();
        setBuzzer(true);

        showMessage("ALCOHOL DETECTED", "Gate: CLOSED");
        state = ALCOHOL_BLOCKED;
      } else {
        setBuzzer(false);
        gateServo.write(GATE_OPEN_ANGLE);

        showMessage("NO ALCOHOL", "FREE TO GO");
        state = GATE_OPEN;
      }
    }
  }

  // Alcohol detected: alarm until vehicle leaves
  else if (state == ALCOHOL_BLOCKED) {
    setFan(false);
    closeGate();
    setBuzzer(true);

    // Use raw sensor reading for immediate reset
    if (!rawVehicleDetected()) {
      resetToWaiting();
    }
  }

  // No alcohol: close gate immediately when vehicle leaves
  else if (state == GATE_OPEN) {
    setFan(false);
    setBuzzer(false);

    // Do not apply the 1.5-second arrival filter here
    if (!rawVehicleDetected()) {
      closeGate();

      detectionTiming = false;
      detectionStartTime = 0;
      state = WAITING;

      showMessage("Gate Closed", "Waiting vehicle");
    }
  }
}
