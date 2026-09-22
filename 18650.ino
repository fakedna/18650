#include "Adafruit_INA3221.h"
#include <Wire.h>
#include <microLCD.h>

#define PRINT_DEC_POINTS 3
#define DEBUG_MODE false

const uint8_t LCD_ADDR = 0x27;  //или 0х57
const uint8_t INA3221_ADDR40_GND = 0x40;
const uint8_t INA3221_ADDR41_VCC = 0x41;
const uint8_t PIN_BUTTON = 6;
const uint8_t PIN_Q[4] = { 5, 4, 3, 2 };

Adafruit_INA3221 ina3221_1;
Adafruit_INA3221 ina3221_2;
microLCD lcd(LCD_ADDR);  //Create object for display

float current[4] = { 0, 0, 0, 0 };
float voltage[4] = { 0, 0, 0, 0 };
float capacity[4] = { 0, 0, 0, 0 };
float ocv_voltage[4] = { 0, 0, 0, 0 };
float resistances[4] = { 0, 0, 0, 0 };
float times[4] = { 0, 0, 0, 0 };
int states[4] = { 0, 0, 0, 0 };  // 0-EMPTY, 1-CHG, 2-DSC, 3-CHG FIN, 4-DFN

int selectedChannel = 0;
int lastSelectedChannel = 0;
int lastButtonState = LOW;
int buttonState;

unsigned long lastScreenTime = 0;
unsigned long screenDelay = 500;
unsigned long lastMeasureTime = 0;
unsigned long measureDelay = 500;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

const float CUTOFF_VOLTAGE = 2.8;
const float CUTOFF_CURRENT = 0.1;
const float NO_BATTERY_VOLTAGE = 0.5;        // Voltage below which we consider that there is no battery
const float MIN_DISCHARGE_CURRENT = -0.02;   // Minimum discharge current
const float RESISTANCE_MEASURE_DELAY = 2.0;  // Delay in seconds before measuring internal resistance

void setup() {
  for (int i = 0; i < 4; i++) {
    uint8_t pin = PIN_Q[i];
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.begin(115200);
  while (!Serial) delay(10);

  SetupIna(ina3221_1, INA3221_ADDR41_VCC);
  SetupIna(ina3221_2, INA3221_ADDR40_GND);
  lcd.begin(LCD_4BITMODE, LCD_2LINE, LCD_5x8DOTS);

  lcd.setCursor(0, 0);
  lcd.print(" Charger 18650 ");
  lcd.setCursor(0, 1);
  lcd.print("  version 1.0  ");
  delay(500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Init complete! ");
  Serial.println("Init complete!");
  delay(500);
}

void loop() {

  unsigned long currentTime = millis();

  ReadData(currentTime);

  CheckState(currentTime);

  CheckButton(currentTime);

  UpdateScreen(currentTime);
}

void CheckState(unsigned long currentTime) {

  if (currentTime - lastMeasureTime > measureDelay) {
    // Calculate the time delta in seconds
    float deltaTime = (currentTime - lastMeasureTime) / 1000.0;
    lastMeasureTime = currentTime;
    float deltaTime_hours = deltaTime / 3600.0;

    for (int i = 0; i < 4; i++) {
      uint8_t pin = PIN_Q[i];
      int prevState = states[i];
      float voltage_now = voltage[i];
      float current_now = current[i];

      // 1. Check for physical absence of the battery
      if (voltage_now < NO_BATTERY_VOLTAGE) {
        states[i] = 0;
        digitalWrite(pin, LOW);  // Отключаем разрядную нагрузку
        times[i] = 0;
        capacity[i] = 0;
        ocv_voltage[i] = 0;
        resistances[i] = 0;
        continue;
      }

      // 2. Discharge limit (CUTOFF) reached
      if (voltage_now <= CUTOFF_VOLTAGE) {
        digitalWrite(pin, LOW);  // Physically disconnect the load using the MOSFET
        if (prevState == 2) {    // If the battery was previously discharging
          states[i] = 4;         // Status: Discharge finished (DFN)
        }
        continue;  // Move to the next channel
      }

      // 3. Analyze the current operating mode based on current
      if (current_now > CUTOFF_CURRENT) {
        // Active charging
        states[i] = 1;  // CHG
        times[i] += deltaTime;
        digitalWrite(pin, LOW);  // Keep the MOSFET closed just in case

      } else if (current_now < MIN_DISCHARGE_CURRENT) {
        // Switch is set to DISCHARGE mode

        if (prevState != 2) {
          times[i] = 0;
          capacity[i] = 0;
          resistances[i] = -1.0;  // Flag: measurement has not been performed yet
        }

        states[i] = 2;  // DSC
        times[i] += deltaTime;
        capacity[i] += (-current_now * 1000.0) * deltaTime_hours;

        // Measure exactly once at the N-th second of discharge
        if (times[i] >= RESISTANCE_MEASURE_DELAY && resistances[i] < 0) {
          float v_drop = ocv_voltage[i] - voltage_now;
          if (v_drop > 0 && current_now != 0) {
            resistances[i] = (v_drop / -current_now) * 1000.0;

            if (DEBUG_MODE) {
              Serial.print("CH ");
              Serial.print(i);
              Serial.print(": v_drop=");
              Serial.print(v_drop);
              Serial.print(" | I=");
              Serial.print(current_now);
              Serial.print(" | R=");
              Serial.println(resistances[i]);
            }
          } else {
            resistances[i] = 0;
          }
        }

      } else {
        // NO CURRENT (Below CUTOFF_CURRENT and not negative)

        if (prevState == 1) {
          // If the switch is still in the "Charge" position, but the current has dropped — charging is complete
         states[i] = 3;  // CHG FIN
          digitalWrite(pin, LOW);
        } else if (prevState == 2) {
          // If the battery was discharging, but the current disappeared — the switch was turned back or the battery protection was triggered
          states[i] = 4;  // DFN
          digitalWrite(pin, LOW);
        } else if (prevState == 0 || prevState == 3 || prevState == 4) {
          // Waiting for the test to start: continuously update the no-load voltage (OCV)
          ocv_voltage[i] = voltage_now;
          digitalWrite(pin, HIGH);
        }
      }
    }
  }
}

void UpdateScreen(unsigned long currentTime) {

  if (currentTime - lastScreenTime > screenDelay) {

    lastScreenTime = currentTime;

    int i = selectedChannel;
    int state = states[i];
    if (i != lastSelectedChannel) {
      lastSelectedChannel = i;
      lcd.clear();
    }

    // LINE 1: Display channel number, voltage, and current
    lcd.setCursor(0, 0);
    String line1 = "#" + String(i + 1) + " " + String(voltage[i], 2) + "V " + String(current[i], 2) + "A  ";
    lcd.print(line1);

    // LINE 2: Display status and metrics
    lcd.setCursor(0, 1);
    if (state == 0) {
      lcd.print("     EMPTY!     ");
    } else if (state == 1) {
      String line2 = "CHG     " + secondsToHMS(times[i]);
      lcd.print(line2);
    } else if (state == 2 || state == 4) {  // DSC and DFN combined for compactness

      // Convert float to integers
      int capInt = (int)capacity[i];
      int resInt = (int)resistances[i];

      // Prevent display overflow (limit to 9999 mAh and 99 mR)
      if (capInt > 9999) capInt = 9999;
      if (resInt > 99) resInt = 99;
      if (capInt < 0) capInt = 0;
      if (resInt < 0) resInt = 0;

      char buffer[17];
      const char *prefix = (state == 2) ? "DSC" : "DFN";
      sprintf(buffer, "%s %04dmAh %02dmR", prefix, capInt, resInt);

      // Display the prepared string on the screen
      lcd.print(String(buffer));
    } else if (state == 3) {
      String line2 = "CHG FIN " + secondsToHMS(times[i]);
      lcd.print(line2);
    }
  }
}

void ReadData(unsigned long currentTime) {

  current[0] = ina3221_1.getCurrentAmps(0);
  voltage[0] = ina3221_1.getBusVoltage(0);

  current[1] = ina3221_1.getCurrentAmps(1);
  voltage[1] = ina3221_1.getBusVoltage(1);

  current[2] = ina3221_2.getCurrentAmps(0);
  voltage[2] = ina3221_2.getBusVoltage(0);

  current[3] = ina3221_2.getCurrentAmps(1);
  voltage[3] = ina3221_2.getBusVoltage(1);
}

void CheckButton(unsigned long currentTime) {

  int reading = digitalRead(PIN_BUTTON);

  if (reading != lastButtonState) {
    lastDebounceTime = currentTime;
  }

  if (currentTime - lastDebounceTime > debounceDelay) {

    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        selectedChannel++;
        if (selectedChannel >= 4) selectedChannel = 0;
      }
    }
  }
  lastButtonState = reading;
}

void SetupIna(Adafruit_INA3221 &ina3221, const uint8_t &addr) {

  if (!ina3221.begin(addr, &Wire)) {
    Serial.print("Failed to find INA3221 chip on 0x");
    Serial.println(addr, HEX);
    while (1)
      delay(10);
  }
  Serial.print("INA3221 Found on 0x=");
  Serial.println(addr, HEX);

  ina3221.setAveragingMode(INA3221_AVG_16_SAMPLES);

  // Set shunt resistances for all channels to 0.1 ohms
  for (uint8_t i = 0; i < 3; i++) {
    ina3221.setShuntResistance(i, 0.1);
  }

  // Set a power valid alert to tell us if ALL channels are between the two limits:
  ina3221.setPowerValidLimits(3.0 /* lower limit */, 15.0 /* upper limit */);
}

String secondsToHMS(float seconds) {
  int hr = seconds / 3600;                    //Number of seconds in an hour
  int mins = (seconds - hr * 3600) / 60;      //Remove the number of hours and calculate the minutes.
  int sec = seconds - hr * 3600 - mins * 60;  //Remove the number of hours and minutes, leaving only seconds.
  sec %= 60;
  mins %= 60;
  hr %= 24;

  String res = "";
  if (hr < 10) res += "0";
  res += String(hr) + ":";
  if (mins < 10) res += "0";
  res += String(mins) + ":";
  if (sec < 10) res += "0";
  res += String(sec);

  return res;
}
