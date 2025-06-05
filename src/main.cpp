#include <Arduino.h>
#include <WiFi.h>

// ---------------- WiFi and Access Point Settings and Values----------------
// WiFi SSID and password for connecting to an existing network
const char* ssid = "TostiReflow";
const char* password = "LPLTosti";
// Create a server that listens on port 80
WiFiServer server(80); 
// stores the header for the HTTP response
String header;

// ---------------- Thermistor Settings and Values ----------------
// which analog pin to connect
// WARNING: Use ADC1 (GPIO 32 to 39) on ESP32, as ADC2 is used by WiFi and Bluetooth.
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
// milliseconds between samples
#define TIMEBETWEENSAMPLES 10 
// stores the samples
int samples[NUMSAMPLES], average = 0;
// current zero index in the samples array
uint8_t sampleIndex = 0;
// last time we sampled the thermistor
ulong lastSampleTime = 0;
// last temperature in Celsius
float lastTemperature = 0;
// last calculated resistance in Ohms
float resistance = 0;

// ---------------- PID Settings and Values----------------


// ---------------- Debug settings ----------------
// milliseconds between reports
#define REPORTINTERVAL 1000 
// last time we reported
ulong lastReportTime = 0;


// ---------------- Function prototypes ----------------
void HandleAccessPoint();
void HandleThermistor();
void CalculateTemperature();

void setup() {
  Serial.begin(115200);
  
  Serial.println("Starting up Access Point...");
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.begin();
}

void loop() {
  HandleAccessPoint();

  HandleThermistor();

}

void HandleAccessPoint(){
  WiFiClient client = server.available(); // listen for incoming clients

  if (client) {
    Serial.println("New client connected.");
    String currentLine = ""; // make a String to hold incoming data
    while (client.connected()) {
      if (client.available()) {
        char c = client.read(); // read a byte
        Serial.write(c); // echo it back to the serial monitor
        header += c; // add it to the header string

        if (c == '\n') { // if the byte is a newline character
          if (currentLine.length() == 0) { 
            // send a standard HTTP response header
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            
            // send the HTML content
            client.println("<!DOCTYPE html>");
            client.println("<html>");
            client.println("<head><title>Tosti Reflow Oven</title></head>");
            client.println("<body>");
            client.println("<h1>Tosti Reflow Oven</h1>");
            client.println("<p>Thermistor readings:</p>");
            client.print("<p>Average analog reading: ");
            client.print(average);
            client.println("</p>");
            client.print("<p>Thermistor resistance: ");
            client.print(resistance);
            client.println(" Ohms</p>");
            client.print("<p>Temperature: ");
            client.print(lastTemperature);
            client.println(" C</p>");
            
            // break out of the while loop
            break;
          } else { // if you got a newline character, clear the current line
            currentLine = "";
          }
        }
      }
    }
  }
}

void HandleThermistor(){
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
