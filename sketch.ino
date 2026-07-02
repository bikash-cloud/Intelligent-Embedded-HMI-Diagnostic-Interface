#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pins
#define CELL1_PIN 33
#define CELL2_PIN 35
#define CELL3_PIN 34
#define CELL4_PIN 32

#define RELAY_PIN 25
#define BUZZER_PIN 26

// Thresholds
#define WEAK_CELL_LIMIT 1.00
#define OVER_VOLT_LIMIT 3.20
#define SENSOR_MIN_LIMIT 0.02
#define SENSOR_MAX_LIMIT 3.30
#define IMBALANCE_LIMIT 25.0

// Timing
#define ADC_INTERVAL 100
#define ANALYTICS_INTERVAL 200
#define LCD_INTERVAL 300
#define SCREEN_ROTATE_INTERVAL 2000
#define SERIAL_INTERVAL 1000
#define BUZZER_INTERVAL 300
#define RECOVERY_TIME 3000

float cells[4];
float packVoltage = 0;
float averageVoltage = 0;
float maxVoltage = 0;
float minVoltage = 0;
float imbalance = 0;
float soc = 0;

int strongestCell = 1;
int weakestCell = 1;
int faultCell = 0;
int activeFaults = 0;
int currentScreen = 0;

bool weakCellFault = false;
bool overVoltageFault = false;
bool sensorFault = false;
bool imbalanceFault = false;

bool relayState = true;
bool buzzerState = false;
bool wasFaultActive = false;

unsigned long lastADC = 0;
unsigned long lastAnalytics = 0;
unsigned long lastLCD = 0;
unsigned long lastScreenRotate = 0;
unsigned long lastSerial = 0;
unsigned long lastBuzzer = 0;
unsigned long recoveryStartTime = 0;

String systemState = "SAFE";
String lastLine0 = "";
String lastLine1 = "";

// ---------- Utility ----------
float readVoltage(int pin) {
  int adc = analogRead(pin);
  return (adc * 3.3) / 4095.0;
}

void printLCDLine(int row, String text) {
  if (text.length() > 16) {
    text = text.substring(0, 16);
  }

  while (text.length() < 16) {
    text += " ";
  }

  if (row == 0 && text != lastLine0) {
    lcd.setCursor(0, 0);
    lcd.print(text);
    lastLine0 = text;
  }

  if (row == 1 && text != lastLine1) {
    lcd.setCursor(0, 1);
    lcd.print(text);
    lastLine1 = text;
  }
}

String getHealthStatus() {
  if (activeFaults > 0) return "CRITICAL";
  if (imbalance > 15.0) return "WARNING";
  if (soc > 70.0 && imbalance < 10.0) return "EXCELLENT";
  return "GOOD";
}

// ---------- ADC ----------
void readBatteryCells() {
  cells[0] = readVoltage(CELL1_PIN);
  cells[1] = readVoltage(CELL2_PIN);
  cells[2] = readVoltage(CELL3_PIN);
  cells[3] = readVoltage(CELL4_PIN);
}

// ---------- Analytics ----------
void calculateBatteryAnalytics() {
  packVoltage = 0;
  maxVoltage = cells[0];
  minVoltage = cells[0];
  strongestCell = 1;
  weakestCell = 1;

  for (int i = 0; i < 4; i++) {
    packVoltage += cells[i];

    if (cells[i] > maxVoltage) {
      maxVoltage = cells[i];
      strongestCell = i + 1;
    }

    if (cells[i] < minVoltage) {
      minVoltage = cells[i];
      weakestCell = i + 1;
    }
  }

  averageVoltage = packVoltage / 4.0;

  if (averageVoltage > 0) {
    imbalance = ((maxVoltage - minVoltage) / averageVoltage) * 100.0;
  } else {
    imbalance = 0;
  }

  soc = (averageVoltage / 3.3) * 100.0;

  if (soc > 100) soc = 100;
  if (soc < 0) soc = 0;
}

// ---------- Fault Detection ----------
void detectFaults() {
  weakCellFault = false;
  overVoltageFault = false;
  sensorFault = false;
  imbalanceFault = false;
  activeFaults = 0;
  faultCell = 0;

  for (int i = 0; i < 4; i++) {
    if (cells[i] <= SENSOR_MIN_LIMIT || cells[i] >= SENSOR_MAX_LIMIT) {
      sensorFault = true;
      faultCell = i + 1;
    }
    else if (cells[i] < WEAK_CELL_LIMIT) {
      weakCellFault = true;
      faultCell = i + 1;
    }
    else if (cells[i] > OVER_VOLT_LIMIT) {
      overVoltageFault = true;
      faultCell = i + 1;
    }
  }

  if (imbalance > IMBALANCE_LIMIT) {
    imbalanceFault = true;
  }

  if (weakCellFault) activeFaults++;
  if (overVoltageFault) activeFaults++;
  if (sensorFault) activeFaults++;
  if (imbalanceFault) activeFaults++;

  if (activeFaults > 0) {
    systemState = "FAULT";
    relayState = false;
    wasFaultActive = true;
    digitalWrite(RELAY_PIN, LOW);
  } 
  else {
    if (wasFaultActive) {
      systemState = "RECOVERY";

      if (recoveryStartTime == 0) {
        recoveryStartTime = millis();
      }

      if (millis() - recoveryStartTime >= RECOVERY_TIME) {
        systemState = "SAFE";
        relayState = true;
        wasFaultActive = false;
        recoveryStartTime = 0;
        digitalWrite(RELAY_PIN, HIGH);
      } else {
        relayState = false;
        digitalWrite(RELAY_PIN, LOW);
      }
    } 
    else {
      systemState = "SAFE";
      relayState = true;
      digitalWrite(RELAY_PIN, HIGH);
    }
  }
}

// ---------- Buzzer ----------
void updateBuzzer() {
  if (activeFaults > 0) {
    if (millis() - lastBuzzer >= BUZZER_INTERVAL) {
      lastBuzzer = millis();
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
    }
  }
  else if (systemState == "RECOVERY") {
    if (millis() - lastBuzzer >= 700) {
      lastBuzzer = millis();
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
    }
  }
  else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = false;
  }
}

// ---------- LCD Screens ----------
void showFaultOverrideScreen() {
  printLCDLine(0, "CRITICAL FAULT");

  if (sensorFault) {
    printLCDLine(1, "SENSOR C" + String(faultCell));
  }
  else if (weakCellFault) {
    printLCDLine(1, "LOW CELL " + String(faultCell));
  }
  else if (overVoltageFault) {
    printLCDLine(1, "OVERVOLT C" + String(faultCell));
  }
  else if (imbalanceFault) {
    printLCDLine(1, "HIGH IMBALANCE");
  }
}

void showRecoveryScreen() {
  unsigned long remaining = 0;

  if (millis() - recoveryStartTime < RECOVERY_TIME) {
    remaining = (RECOVERY_TIME - (millis() - recoveryStartTime)) / 1000;
  }

  printLCDLine(0, "RECOVERY MODE");
  printLCDLine(1, "Wait " + String(remaining) + " sec");
}

void showScreen0() {
  printLCDLine(0, "C1:" + String(cells[0], 1) + " C2:" + String(cells[1], 1));
  printLCDLine(1, "C3:" + String(cells[2], 1) + " C4:" + String(cells[3], 1));
}

void showScreen1() {
  printLCDLine(0, "PACK:" + String(packVoltage, 1) + "V");
  printLCDLine(1, "AVG:" + String(averageVoltage, 2) + "V");
}

void showScreen2() {
  printLCDLine(0, "SOC:" + String(soc, 1) + "%");
  printLCDLine(1, "IMB:" + String(imbalance, 1) + "%");
}

void showScreen3() {
  printLCDLine(0, "STATE:" + systemState);
  printLCDLine(1, relayState ? "Relay:ON" : "Relay:OFF");
}

void showScreen4() {
  printLCDLine(0, "STR:C" + String(strongestCell) + " WK:C" + String(weakestCell));
  printLCDLine(1, "FLT:" + String(activeFaults));
}

void showScreen5() {
  printLCDLine(0, "HEALTH STATUS");
  printLCDLine(1, getHealthStatus());
}

void updateLCD() {
  if (activeFaults > 0) {
    showFaultOverrideScreen();
    return;
  }

  if (systemState == "RECOVERY") {
    showRecoveryScreen();
    return;
  }

  if (millis() - lastScreenRotate >= SCREEN_ROTATE_INTERVAL) {
    lastScreenRotate = millis();
    currentScreen++;

    if (currentScreen > 5) {
      currentScreen = 0;
    }
  }

  if (currentScreen == 0) showScreen0();
  else if (currentScreen == 1) showScreen1();
  else if (currentScreen == 2) showScreen2();
  else if (currentScreen == 3) showScreen3();
  else if (currentScreen == 4) showScreen4();
  else if (currentScreen == 5) showScreen5();
}

// ---------- Serial ----------
void printSerialDashboard() {
  Serial.println("====================================");
  Serial.println("INTELLIGENT EMBEDDED HMI INTERFACE");
  Serial.println("====================================");

  for (int i = 0; i < 4; i++) {
    Serial.print("Cell ");
    Serial.print(i + 1);
    Serial.print(" : ");
    Serial.print(cells[i], 2);
    Serial.println(" V");
  }

  Serial.println("------------------------------------");

  Serial.print("Pack Voltage   : ");
  Serial.print(packVoltage, 2);
  Serial.println(" V");

  Serial.print("Average Voltage: ");
  Serial.print(averageVoltage, 2);
  Serial.println(" V");

  Serial.print("SOC            : ");
  Serial.print(soc, 1);
  Serial.println(" %");

  Serial.print("Imbalance      : ");
  Serial.print(imbalance, 2);
  Serial.println(" %");

  Serial.print("Strongest Cell : Cell ");
  Serial.println(strongestCell);

  Serial.print("Weakest Cell   : Cell ");
  Serial.println(weakestCell);

  Serial.print("System State   : ");
  Serial.println(systemState);

  Serial.print("Health Status  : ");
  Serial.println(getHealthStatus());

  Serial.print("Relay Status   : ");
  Serial.println(relayState ? "ON" : "OFF");

  Serial.print("Active Faults  : ");
  Serial.println(activeFaults);

  Serial.print("Weak Cell      : ");
  Serial.println(weakCellFault ? "YES" : "NO");

  Serial.print("Over Voltage   : ");
  Serial.println(overVoltageFault ? "YES" : "NO");

  Serial.print("Sensor Fault   : ");
  Serial.println(sensorFault ? "YES" : "NO");

  Serial.print("Imbalance Fault: ");
  Serial.println(imbalanceFault ? "YES" : "NO");

  Serial.println();
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();

  printLCDLine(0, "Embedded HMI");
  printLCDLine(1, "Diagnostics");

  readBatteryCells();
  calculateBatteryAnalytics();
  detectFaults();
}

// ---------- Loop ----------
void loop() {
  unsigned long now = millis();

  if (now - lastADC >= ADC_INTERVAL) {
    lastADC = now;
    readBatteryCells();
  }

  if (now - lastAnalytics >= ANALYTICS_INTERVAL) {
    lastAnalytics = now;
    calculateBatteryAnalytics();
    detectFaults();
  }

  if (now - lastLCD >= LCD_INTERVAL) {
    lastLCD = now;
    updateLCD();
  }

  if (now - lastSerial >= SERIAL_INTERVAL) {
    lastSerial = now;
    printSerialDashboard();
  }

  updateBuzzer();
}
