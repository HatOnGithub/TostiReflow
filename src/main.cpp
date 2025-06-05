#include <Arduino.h>
// SPDX-FileCopyrightText: 2011 Limor Fried/ladyada for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Thermistor Example #3 from the Adafruit Learning System guide on Thermistors 
// https://learn.adafruit.com/thermistor/overview by Limor Fried, Adafruit Industries
// MIT License - please keep attribution and consider buying parts from Adafruit

// which analog pin to connect
#define THERMISTORPIN 32      
// resistance at 25 degrees C
#define THERMISTORNOMINAL 100000      
// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25   
// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 5
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 4267
// the value of the 'other' resistor
#define SERIESRESISTOR 5450    
// for ESP32, the ADC max value is 4095 (12-bit resolution)
#define ADC_MAX_VALUE 4095 

#define TIMEBETWEENSAMPLES 10 // milliseconds between samples

#define REPORTINTERVAL 1000 // milliseconds between reports

int samples[NUMSAMPLES] = {0,0,0,0,0}, average = 0;

uint8_t sampleIndex = 0;

ulong lastSampleTime = 0, lastReportTime = 0;

float lastTemperature = 0, resistance = 0;

void CalculateTemperature();

void setup() {
  Serial.begin(115200);
  
}

void loop() {

  if (millis() - lastSampleTime >= TIMEBETWEENSAMPLES) {
    lastSampleTime = millis();
    
    // take a reading
    int reading = analogRead(THERMISTORPIN);
    
    // add to the array of samples
    samples[sampleIndex++ % NUMSAMPLES] = reading;
    
    int _avg = 0;
    for (int i = 0; i < NUMSAMPLES; i++)
      _avg += samples[i];
    
    average = _avg / NUMSAMPLES;
    
    CalculateTemperature();
  }

  if (millis() - lastReportTime >= REPORTINTERVAL) {
    lastReportTime = millis();
    
    Serial.print("Average analog reading "); 
    Serial.println(average);

    Serial.print("Thermistor resistance "); 
    Serial.println(resistance);
      
    Serial.print("Temperature "); 
    Serial.print(lastTemperature);
    Serial.println(" *C");
  }

}

void CalculateTemperature(){
  float _res = (float)average;
  if (_res <= 0) _res = 1; // prevent division by zero
  // convert the value to resistance
  _res = ADC_MAX_VALUE / _res - 1;
  if (_res <= 0) _res = 0.0001; // prevent division by zero
  _res = SERIESRESISTOR / _res;
  resistance = _res; // store the resistance value
  
  float steinhart;
  steinhart = resistance / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert absolute temp to C

  lastTemperature = steinhart;
  
}
