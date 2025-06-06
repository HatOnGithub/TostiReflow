#include <Arduino.h>
#include <Esp.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PID_v1.h>
#include "LittleFS.h"

// ---------------- WiFi and Access Point Settings and Values----------------
// WiFi SSID and password for connecting to an existing network
const char* ssid = "TostiReflow";
const char* password = "LPLTosti";
// Create a server that listens on port 80
WebServer server(80); 
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
#define RELAYPIN 23 // pin to control the relay
#define STOPBTN 34
#define STARTBTN 35
unsigned long timeSinceReflowStarted, reflowStarted;
unsigned long timeTempCheck = 1000;
unsigned long lastTimeTempCheck = 0;
double preheatTemp = 100, soakTemp = 150, reflowTemp = 230, cooldownTemp = 25;
unsigned long preheatTime = 120000, soakTime = 60000, reflowTime = 120000, cooldownTime = 120000, totalTime = preheatTime + soakTime + reflowTime + cooldownTime;
bool preheating = false, soaking = false, reflowing = false, coolingDown = false, newState = false;
bool start = false;

double Input, Output, Setpoint; // PID variables
double Kp=2, Ki=5, Kd=1;
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

// ---------------- Debug settings ----------------
// milliseconds between reports
#define REPORTINTERVAL 1000 
// last time we reported
ulong lastReportTime = 0;


// ---------------- Function prototypes ----------------
void HandleButtons();
void HandlePID();

void Handle_OnConnect();
void Handle_GetCSS();
void Handle_GetJS();
void Handle_NotFound();

void HandleThermistor();
void CalculateTemperature();

// --------------- Setup and Loop ----------------
void setup() {
  Serial.begin(115200);
  
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  uint32_t totalUsed = LittleFS.usedBytes() + ESP.getSketchSize();

  Serial.println("LittleFS Mounted Successfully");
  Serial.print("Total Storage: ");
  Serial.print(totalUsed);
  Serial.print(" / ");
  Serial.print(ESP.getFlashChipSize());
  Serial.print(" (" + String((float)totalUsed / (float)ESP.getFlashChipSize() * 100, 2) + "%)");
  Serial.println(" bytes used");

  Serial.println("File System Storage:");
  Serial.print(LittleFS.usedBytes());
  Serial.print(" / ");
  Serial.print(LittleFS.totalBytes());
  Serial.print(" (" + String((float)LittleFS.usedBytes() / (float)LittleFS.totalBytes() * 100, 2) + "%)");
  Serial.println(" bytes used");

  Serial.println("Starting up Access Point...");
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.on("/", HTTP_GET, Handle_OnConnect);
  server.on("/style.css", HTTP_GET, Handle_GetCSS);
  server.on("/main.js", HTTP_GET, Handle_GetJS);
  server.onNotFound(Handle_NotFound);

  server.begin();

  Setpoint = cooldownTemp;
  // tell the PID to range between 0 and the full window size
  myPID.SetOutputLimits(0, 1);

  // turn the PID on
  myPID.SetMode(AUTOMATIC);

  pinMode(RELAYPIN, OUTPUT);
  pinMode(STOPBTN, INPUT_PULLUP);
  pinMode(STARTBTN, INPUT_PULLUP);
  digitalWrite(RELAYPIN, LOW);
}

void loop() {
  server.handleClient(); // handle incoming client requests

  HandleButtons();

  HandlePID();

  HandleThermistor();

}

void HandleButtons() {
  if (digitalRead(STOPBTN) == LOW) {
    if (start) {
      Serial.println("Stopping reflow process.");
      reflowing = false;
      coolingDown = false;
      soaking = false;
      preheating = false;
      start = false;
      digitalWrite(RELAYPIN, 0);
    }
  }

  if (digitalRead(STARTBTN) == LOW) {
    if (!start) {
      Serial.println("Starting reflow process.");
      reflowing = false;
      coolingDown = false;
      soaking = false;
      preheating = true;
      start = true;
      reflowStarted = millis();
    }
  }
}

void HandlePID(){

  if (!start) return; // do nothing if not started

  timeSinceReflowStarted = millis() - reflowStarted;

  if (timeSinceReflowStarted - lastTimeTempCheck > timeTempCheck){
    lastTimeTempCheck = timeSinceReflowStarted;

    Input = lastTemperature; // read the temperature from the thermistor
    myPID.Compute(); // compute the PID output
    
    if (Output > 0.5) {
      digitalWrite(RELAYPIN, HIGH); // turn on the relay
    } else {
      digitalWrite(RELAYPIN, LOW); // turn off the relay
    }

    Serial.println("PID Output: " + String(Output) + ", Setpoint: " + String(Setpoint) + ", Input: " + String(Input));
  }
  
  if(timeSinceReflowStarted > (preheatTime + soakTime + reflowTime)){ // preheat and soak and reflow are complete. Start cooldown
    if(!coolingDown){
      newState = true;
      preheating = false, soaking = false, reflowing = false, coolingDown = true;
    }
    Setpoint = cooldownTemp;
  }
  else if(timeSinceReflowStarted > (preheatTime + soakTime)){ // preheat and soak are complete. Start reflow
    if(!reflowing){
      newState = true;
      preheating = false, soaking = false, reflowing = true, coolingDown = false;
    }
    Setpoint = reflowTemp;
  }
  else if(timeSinceReflowStarted > preheatTime){ // preheat is complete. Start soak
    if(!soaking){
      newState = true;
      preheating = false, soaking = true, reflowing = false, coolingDown = false;
    }
    Setpoint = soakTemp;
  }
  else{ // cycle is starting. Start preheat
    if(!preheating){
      newState = true;
      preheating = true, soaking = false, reflowing = false, coolingDown = false;
    }
    Setpoint = preheatTemp;
  }
}

void Handle_OnConnect(){
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void Handle_GetCSS(){
  File file = LittleFS.open("/style.css", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/css");
  file.close();
}

void Handle_GetJS(){
  File file = LittleFS.open("/main.js", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/javascript");
  file.close();
}

void Handle_NotFound(){
  Serial.println("Not Found: " + server.uri());
  server.send(404, "text/plain", "Not Found");
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
