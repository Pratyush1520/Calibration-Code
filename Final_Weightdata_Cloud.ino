#include <WiFi.h>
#include "ThingSpeak.h"
#include <HX711_ADC.h>
#if defined(ESP8266) || defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif

// HX711 constructor:
const int HX711_dout = 21; // MCU > HX711 dout pin
const int HX711_sck = 22; // MCU > HX711 sck pin
HX711_ADC LoadCell(HX711_dout, HX711_sck);

const int calVal_eepromAdress = 0;
unsigned long t = 0;

const char *ssid = "Xiaomi 11i";   // your network SSID (name)
const char *password = "pratyusH20"; // your network password
WiFiClient client;

unsigned long myChannelNumber = 2226698;
const char *myWriteAPIKey = "9IJZQX45X573DJAA";

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 5000;

// Variable to hold weight readings
float lastWeight = 0;
boolean isStable = false;

boolean sendData = false; // Flag to control sending data to cloud, set to false to pause data sending

float calibrationFactor = 1.0; // Initial calibration factor

void setup()
{
  Serial.begin(115200); // Initialize serial
  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client); // Initialize ThingSpeak
  LoadCell.begin();
  unsigned long stabilizingtime = 2000; // precision right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true;                 // set this to false if you don't want tare to be performed in the next step
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag())
  {
    Serial.println("Timeout, check MCU > HX711 wiring and pin designations");
    while (1)
      ;
  }
  else
  {
    Serial.println("Startup is complete");
  }

  // Check if calibration value is stored in EEPROM
#if defined(ESP8266) || defined(ESP32)
  EEPROM.begin(512);
#endif
  EEPROM.get(calVal_eepromAdress, calibrationFactor);
  if (calibrationFactor == 0)
  {
    Serial.println("Calibration value not found in EEPROM. Starting calibration...");
    calibrate(); // start calibration procedure if not found
    EEPROM.put(calVal_eepromAdress, calibrationFactor); // Store the calibration value in EEPROM
#if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
#endif
  }
  else
  {
    Serial.print("Calibration value found in EEPROM: ");
    Serial.println(calibrationFactor);
  }
  // Calibration complete, so start the data sending loop
  sendData = true;
}

void loop()
{
  // Check for new data/start next conversion for the HX711 sensor
  if (LoadCell.update())
  {
    float weight = LoadCell.getData() / calibrationFactor; // Use calibrationFactor for weight calculation

    // Check for stability by comparing consecutive weight readings
    if (fabs(weight - lastWeight) < 0.3) // Adjust the threshold as needed
    {
      isStable = true;
    }
    else
    {
      isStable = false;
    }

    lastWeight = weight;
  }

  // If sendData flag is true, and weight is stable, and the specified time interval has passed, send data to ThingSpeak
  if (sendData && isStable && (millis() - lastTime) > timerDelay)
  {
    // Connect or reconnect to WiFi
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.print("Attempting to connect");
      while (WiFi.status() != WL_CONNECTED)
      {
        WiFi.begin(ssid, password);
        delay(5000);
      }
      Serial.println("\nConnected.");
    }

    // Send the weight value to ThingSpeak
    ThingSpeak.setField(1, lastWeight);

    // Send the data to ThingSpeak
    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (x == 200)
    {
      Serial.println("Channel update successful.");
    }
    else
    {
      Serial.println("Problem updating channel. HTTP error code " + String(x));
    }

    // Reset the timer
    lastTime = millis();

    // Pause data sending until the next timer interval
    sendData = false;
  }

  // If data sending is paused, check if it's time to resume data sending
  if (!sendData && (millis() - lastTime) > timerDelay)
  {
    sendData = true; // Set the flag to true to resume data sending
  }
}

void calibrate()
{
  Serial.println("***");
  Serial.println("Start calibration:");
  Serial.println("Place the load cell on a level stable surface.");
  Serial.println("Remove any load applied to the load cell.");
  Serial.println("Send 't' from the serial monitor to set the tare offset.");

  boolean _resume = false;
  while (_resume == false)
  {
    LoadCell.update();
    if (Serial.available() > 0)
    {
      if (Serial.available() > 0)
      {
        char inByte = Serial.read();
        if (inByte == 't')
          LoadCell.tareNoDelay();
      }
    }
    if (LoadCell.getTareStatus() == true)
    {
      Serial.println("Tare complete");
      _resume = true;
    }
  }

  Serial.println("Now, place your known mass on the load cell.");
  Serial.println("Then send the weight of this mass (e.g., 100.0) from the serial monitor.");

  float known_mass = 0;
  _resume = false;
  while (_resume == false)
  {
    LoadCell.update();
    if (Serial.available() > 0)
    {
      known_mass = Serial.parseFloat();
      if (known_mass != 0)
      {
        Serial.print("Known mass is: ");
        Serial.println(known_mass);
        _resume = true;
      }
    }
  }

  LoadCell.refreshDataSet();                           // Refresh the dataset to be sure that the known mass is measured correctly
  calibrationFactor = LoadCell.getNewCalibration(known_mass); // Get the new calibration value

  Serial.print("New calibration value has been set to: ");
  Serial.println(calibrationFactor);
  Serial.println("Calibration completed.");

  Serial.print("Save this value to EEPROM address ");
  Serial.print(calVal_eepromAdress);
  Serial.println("? y/n");

  _resume = false;
  while (_resume == false)
  {
    if (Serial.available() > 0)
    {
      char inByte = Serial.read();
      if (inByte == 'y')
      {
#if defined(ESP8266) || defined(ESP32)
        EEPROM.begin(512);
#endif
        EEPROM.put(calVal_eepromAdress, calibrationFactor);
#if defined(ESP8266) || defined(ESP32)
        EEPROM.commit();
#endif
        Serial.print("Value ");
        Serial.print(calibrationFactor);
        Serial.print(" saved to EEPROM address: ");
        Serial.println(calVal_eepromAdress);
        _resume = true;
      }
      else if (inByte == 'n')
      {
        Serial.println("Value not saved to EEPROM");
        _resume = true;
      }
    }
  }

  Serial.println("End calibration");
  Serial.println("***");
}