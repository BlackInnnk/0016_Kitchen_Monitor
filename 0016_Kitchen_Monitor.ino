 * Kitchen Safety Monitor
 * Board: MKR1010
 * Sensors: DHT22 / MiCS-4514 / LDR
 * Output: OLED + Buzzer

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include "DFRobot_MICS.h"

/*********** Pin setup ***********/
#define DHTPIN        2       // DHT22 data
#define DHTTYPE       DHT22
#define LDR_PIN       A0      // light sensor
#define BUZZER_PIN    5       // buzzer

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// warm-up time (minutes)
#define CALIBRATION_TIME   1
#define MICS_I2C_ADDRESS   MICS_ADDRESS_0

/*********** Thresholds ***********/
const float TEMP_ALARM_THRESHOLD = 45.0;  // high temp
const int   GAS_HIGH_THRESHOLD   = 90;    // strong gas
const int   GAS_MEDIUM_THRESHOLD = 40;    // gas + dark
const int   LDR_DARK_THRESHOLD   = 200;   // low light
const int   OX_ABSOLUTE_LIMIT    = 100;   // sensor error / dirty air

/*********** Objects ***********/
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT              dht(DHTPIN, DHTTYPE);
DFRobot_MICS_I2C mics(&Wire, MICS_I2C_ADDRESS);

/*********** State vars ***********/
int16_t oxBase = -1;        // gas baseline
bool    micsReady = false;

// alarm needs to stay for a while
int alarmCounter = 0;
const int ALARM_PERSISTENCE_COUNT = 3;

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  Serial.begin(115200);
  Wire.begin();

  // OLED start
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("System Init...");
  display.display();

  // DHT start
  dht.begin();

  // wait for MiCS
  while (!mics.begin()) {
    display.clearDisplay();
    display.setTextSize(2);
    display.println("MiCS ERROR!");
    display.display();
    tone(BUZZER_PIN, 2000); delay(100); noTone(BUZZER_PIN);
    delay(1000);
  }

  // wake sensor if sleeping
  if (mics.getPowerState() == SLEEP_MODE) {
    mics.wakeUpMode();
  }

  // warm-up countdown
  display.clearDisplay();
  display.println("Pre-heating...");
  for (int i = CALIBRATION_TIME * 60; i > 0; i--) {
    display.setCursor(50, 20);
    display.setTextSize(2);
    display.print(i);
    display.display();
    delay(1000);
    display.fillRect(50, 20, 50, 20, SSD1306_BLACK);
  }

  // set baseline
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += mics.getADCData(OX_MODE);
  }
  oxBase = sum / 10;
  micsReady = true;

  // ready beep
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(30, 25);
  display.println("READY!");
  display.display();
  tone(BUZZER_PIN, 2000); delay(100); noTone(BUZZER_PIN);
}

void loop() {
  // read sensors
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int ldrVal = analogRead(LDR_PIN);
  int16_t ox = mics.getADCData(OX_MODE);

  // update baseline only if air cleaner
  if (micsReady && ox > oxBase) {
    oxBase = ox;
  }

  // relative gas change
  int gasDiff = 0;
  if (micsReady && ox > 0) {
    gasDiff = oxBase - ox;
    if (gasDiff < 0) gasDiff = 0;
  }

  // check alarm rules
  bool alarmTriggered = false;
  String alarmMsg = "Safe";

  if (gasDiff > GAS_HIGH_THRESHOLD) {
    alarmTriggered = true;
    alarmMsg = "GAS HIGH!";
  }
  else if (!isnan(t) && t > TEMP_ALARM_THRESHOLD) {
    alarmTriggered = true;
    alarmMsg = "TEMP HIGH!";
  }
  else if (micsReady && ox < OX_ABSOLUTE_LIMIT) {
    alarmTriggered = true;
    alarmMsg = "AIR DIRTY!";
  }
  else if (ldrVal < LDR_DARK_THRESHOLD && gasDiff > GAS_MEDIUM_THRESHOLD) {
    alarmTriggered = true;
    alarmMsg = "CHECK STOVE!";
  }

  // time filter
  if (alarmTriggered) {
    if (alarmCounter < ALARM_PERSISTENCE_COUNT) alarmCounter++;
  } else {
    alarmCounter = 0;
  }

  bool finalAlarm = (alarmCounter >= ALARM_PERSISTENCE_COUNT);

  // buzzer
  if (finalAlarm) {
    tone(BUZZER_PIN, 1000);
    delay(100);
    noTone(BUZZER_PIN);
    delay(100);
  }

  // OLED display
  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(finalAlarm ? alarmMsg : "Safe");

  display.setTextSize(1);
  display.setCursor(0, 26);
  display.print("T:"); display.print(t, 0);
  display.print("C H:"); display.print(h, 0); display.print("%");

  display.setCursor(0, 38);
  display.print("GasDiff: "); display.print(gasDiff);

  display.setCursor(0, 50);
  display.print("Light: "); display.print(ldrVal);
  if (ldrVal < LDR_DARK_THRESHOLD) display.print(" Dark");

  display.display();

  delay(finalAlarm ? 100 : 500);
}