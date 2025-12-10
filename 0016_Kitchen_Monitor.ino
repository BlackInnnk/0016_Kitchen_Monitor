/******************************************************
 * Kitchen Safety Monitor - Final v6.0
 * HW: MKR1010 + DHT22 + MiCS-4514 + OLED + LDR + Buzzer
 * Key Funcs:
 * 1. Alarm time check (1.5s delay to avoid err)
 * 2. Multi-sensor: Dark + Gas (CHECK STOVE!)
 * 3. Dynamic base calib (auto adjust env)
 * 4. Fix OLED UI text overlap
 *****************************************************/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include "DFRobot_MICS.h"

/*********** 0. HW Pins ***********/
#define DHTPIN        2           
#define DHTTYPE       DHT22
#define LDR_PIN       A0          
#define BUZZER_PIN    5           

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// warmup time: use 3 min for real usage
#define CALIBRATION_TIME   1      
#define MICS_I2C_ADDRESS   MICS_ADDRESS_0

/*********** 1. Smart Thresholds ***********/

const float  TEMP_ALARM_THRESHOLD   = 45.0;   // temp limit (30 for demo)
const int    GAS_HIGH_THRESHOLD     = 90;     // high gas limit (ignore wind noise)
const int    GAS_MEDIUM_THRESHOLD   = 40;     // med gas for dark mode
const int    LDR_DARK_THRESHOLD     = 200;    // dark limit (lower = darker)
const int    OX_ABSOLUTE_LIMIT      = 100;    // abs low limit (air too dirty)

/*********** 2. Global Vars ***********/
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT              dht(DHTPIN, DHTTYPE);
DFRobot_MICS_I2C mics(&Wire, MICS_I2C_ADDRESS);

int16_t oxBase     = -1;   
bool    micsReady  = false;

// alarm counter, wait 3 loops (approx 1.5s) to confirm alarm
int     alarmCounter = 0;
const int ALARM_PERSISTENCE_COUNT = 3; 

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  Serial.begin(115200);
  Wire.begin();

  // --- Init OLED ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;); 
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("System Init..."));
  display.display();
  
  // --- Init DHT ---
  dht.begin();
  
  // --- Check MiCS conn & warmup ---
  while (!mics.begin()) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println(F("MiCS ERROR!")); 
    display.display();
    tone(BUZZER_PIN, 2000); delay(100); noTone(BUZZER_PIN);
    delay(1000);
  }
  if (mics.getPowerState() == SLEEP_MODE) mics.wakeUpMode();
  
  // warmup countdown
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Pre-heating..."));
  for (int i = CALIBRATION_TIME * 60; i > 0; i--) {
    display.setCursor(50, 20);
    display.setTextSize(2);
    display.print(i);
    display.display();
    delay(1000);
    display.fillRect(50, 20, 50, 20, SSD1306_BLACK); 
  }

  // get start base val
  long sumOx = 0;
  for (int i = 0; i < 10; i++) sumOx += mics.getADCData(OX_MODE);
  oxBase = sumOx / 10;
  micsReady = true;
  
  // Ready
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(30, 25);
  display.println(F("READY!"));
  display.display();
  tone(BUZZER_PIN, 2000); delay(100); noTone(BUZZER_PIN);
  delay(1000);
}

void loop() {
  // 1. read sensors
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int ldrVal = analogRead(LDR_PIN);
  int16_t ox = mics.getADCData(OX_MODE);

  // 2. auto calib (update base if air cleaner)
  if (micsReady && ox > oxBase) oxBase = ox; 

  // 3. calc pollution diff
  int gasDiff = 0;
  if (micsReady && ox > 0) {
    gasDiff = oxBase - ox;
    if (gasDiff < 0) gasDiff = 0;
  }

  // 4. Check Alarm triggers
  bool alarmTriggered = false; 
  String alarmMsg = "Safe"; 

  // Cond 1: heavy gas/smoke (GAS HIGH)
  if (gasDiff > GAS_HIGH_THRESHOLD) {
    alarmTriggered = true;
    alarmMsg = "GAS HIGH!";
  }
  // Cond 2: high temp (TEMP HIGH)
  else if (!isnan(t) && t > TEMP_ALARM_THRESHOLD) {
    alarmTriggered = true;
    alarmMsg = "TEMP HIGH!";
  }
  // Cond 3: air too dirty/error
  else if (micsReady && ox < OX_ABSOLUTE_LIMIT) {
    alarmTriggered = true;
    alarmMsg = "AIR DIRTY!";
  }
  // Cond 4: dark + some gas (CHECK STOVE)
  else if (ldrVal < LDR_DARK_THRESHOLD && gasDiff > GAS_MEDIUM_THRESHOLD) {
    alarmTriggered = true;
    alarmMsg = "CHECK STOVE!"; 
  }

  // --- check time (need 3 times to beep) ---
  if (alarmTriggered) {
    if (alarmCounter < ALARM_PERSISTENCE_COUNT) alarmCounter++;
  } else {
    alarmCounter = 0;
  }
  
  bool finalAlarm = (alarmCounter >= ALARM_PERSISTENCE_COUNT);


  // 5. Buzzer beep
  if (finalAlarm) {
    tone(BUZZER_PIN, 1000); 
    delay(100); 
    noTone(BUZZER_PIN);
    delay(100); 
  } else {
    noTone(BUZZER_PIN);
  }

  // 6. OLED UI (fixed overlap)
  display.clearDisplay();
  
  // Status bar (Big font)
  display.setTextSize(2);
  display.setCursor(0, 0); 
  display.print(finalAlarm ? alarmMsg : "Safe"); 
  
  // Data area (Small font)
  display.setTextSize(1);
  
  // Temp & Hum (Y=26 for space)
  display.setCursor(0, 26); 
  display.print(F("T:")); display.print(t, 0); display.print(F("C  H:")); display.print(h, 0); display.print(F("%"));
  
  // Gas (Y=38)
  display.setCursor(0, 38); 
  display.print(F("GasDiff: ")); display.print(gasDiff);
  
  // Light (Y=50)
  display.setCursor(0, 50); 
  display.print(F("Light:   ")); display.print(ldrVal);
  if (ldrVal < LDR_DARK_THRESHOLD) display.print(F(" (Dark)"));

  display.display();
  
  // refresh rate
  delay(finalAlarm ? 100 : 500); 
}