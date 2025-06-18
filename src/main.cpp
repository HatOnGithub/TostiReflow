#include <Arduino.h>
#include <Esp.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PID_v1.h>
#include <EEPROM.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <Wire.h> 
#include <SSD1306Wire.h>

// ---------------- Stored Profiles and Settings EEPROM adresses ----------------

const int MaxProfiles = 20; // maximum number of profiles
int ProfileCount = 0; // current number of profiles
String ProfileFolderPrefix = "/profiles"; // folder prefix for profiles
String ProfileNames[MaxProfiles]; // array to store profile names
String CurrentProfileName = "Custom Profile"; // currently loaded profile name

// eeprom addresses for storing last used profile settings and PID tuning values
// all used datatypes are 8 bytes long

const int EEPROM_PREHEAT_TEMP_ADDR = 0; // address to store preheat temperature
const int EEPROM_PREHEAT_TIME_ADDR = 8; // address to store preheat time
const int EEPROM_SOAK_TEMP_ADDR = 16; // address to store soak temperature
const int EEPROM_SOAK_TIME_ADDR = 24; // address to store soak time
const int EEPROM_REFLOW_TEMP_ADDR = 32; // address to store reflow temperature
const int EEPROM_REFLOW_TIME_ADDR = 40; // address to store reflow time
const int EEPROM_COOLDOWN_TEMP_ADDR = 48; // address to store cooldown temperature
const int EEPROM_COOLDOWN_TIME_ADDR = 56; // address to store cooldown time

// eeprom addresses for storing PID tuning values
const int EEPROM_KP_ADDR = 64; // address to store Kp value
const int EEPROM_KI_ADDR = 72; // address to store Ki value
const int EEPROM_KD_ADDR = 80; // address to store Kd value

const int EEPROM_FIRST_RUN = 88; // flag to indicate if this is the first run

// Given that strings are of unknown length, we store the last used profile name in EEPROM
// after everything else, so it does not interfere with the other settings.
const int EEPROM_LASTPROFILE_NAME_ADDR = 89; // address to store the last used profile name


// ---------------- WiFi and Access Point Settings and Values ----------------
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
#define NUMSAMPLES 25
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 4267
// the value of the 'other' resistor
#define SERIESRESISTOR 5450    
// for ESP32, the ADC max value is 4095 (12-bit resolution)
#define ADC_MAX_VALUE 4095 

// milliseconds between samples
int timeBetweenSamples = 10; 
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

unsigned long timeTempCheck = 250; 
unsigned long lastTimeTempCheck = 0;

double 
  preheatTemp = 100, 
  soakTemp = 150, 
  reflowTemp = 230, 
  cooldownTemp = 25;

unsigned long 
  preheatTime = 120000, 
  soakTime = 60000, 
  reflowTime = 120000, 
  cooldownTime = 120000, 
  totalTime = preheatTime + soakTime + reflowTime + cooldownTime;

bool 
  preheating = false, 
  soaking = false, 
  reflowing = false, 
  coolingDown = false, 
  newState = false,
  start = false;

double Input, Output, Setpoint; // PID variables

// tune following this guide: https://tlk-energy.de/blog-en/practical-pid-tuning-guide
double Kp=0.05, Ki=0, Kd=0.005, bias = 0;

PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

// Slow PWM Settings and Values
#define PWM_PERIOD 500 // frequency of the PWM signal
#define PWM_STEPS 10

unsigned long lastPeriod = 0; // last time the PWM signal was updated
unsigned long dutyCycleStep = PWM_PERIOD / PWM_STEPS; // how much to increase the duty cycle each step

// ---------------------- Display Settings----------------------------
SSD1306Wire display(0x3c, 16, 17); // I2C address, SDA, SCL pins
unsigned long lastRefresh, refreshTime = 100;


// ---------------- Function prototypes ----------------
void SaveSettings();
void LoadSettings();
void PutString(int adr, String str);
String GetString(int adr);

void SetupFS();
void SetupAP();
void SetupPID();
void SetupDisplay();

void HandleButtons();
void HandleDisplay();
void HandlePID();
void HandleSlowPWM();
void HandleThermistor();
void CalculateTemperature();
void UpdateProfileList();
void HandleSerialCommands();

void OnConnect();
void SetProfileValues();
void SetPIDValues();
void GetProfiles();
void SaveProfile();
void DeleteProfile();
void LoadProfile();
void GetStatus();
void NotFound();


// --------------- Setup and Loop ----------------
void setup() {
  Serial.begin(115200);

  Serial.println("First Run Flag: " + String(EEPROM.read(EEPROM_FIRST_RUN)));

  LoadSettings();
  SetupFS();
  SetupAP();
  SetupPID();
  SetupDisplay();
}

void loop() {
  server.handleClient(); // handle incoming client requests
  HandleButtons();
  HandleDisplay();
  HandlePID();
  HandleSlowPWM();
  HandleThermistor();
  HandleSerialCommands();
}

// ===================================================================
// |                     Function Definitions                        |
// ===================================================================

// Save the current settings to EEPROM
void SaveSettings() {
  Serial.println("Saving settings to EEPROM...");
  EEPROM.begin(512); // initialize EEPROM with size 512 bytes

  EEPROM.put(EEPROM_PREHEAT_TEMP_ADDR, preheatTemp);
  EEPROM.put(EEPROM_PREHEAT_TIME_ADDR, preheatTime);
  EEPROM.put(EEPROM_SOAK_TEMP_ADDR, soakTemp);
  EEPROM.put(EEPROM_SOAK_TIME_ADDR, soakTime);
  EEPROM.put(EEPROM_REFLOW_TEMP_ADDR, reflowTemp);
  EEPROM.put(EEPROM_REFLOW_TIME_ADDR, reflowTime);
  EEPROM.put(EEPROM_COOLDOWN_TEMP_ADDR, cooldownTemp);
  EEPROM.put(EEPROM_COOLDOWN_TIME_ADDR, cooldownTime);

  totalTime = preheatTime + soakTime + reflowTime + cooldownTime; // update total time

  EEPROM.put(EEPROM_KP_ADDR, Kp);
  EEPROM.put(EEPROM_KI_ADDR, Ki);
  EEPROM.put(EEPROM_KD_ADDR, Kd);

  PutString(EEPROM_LASTPROFILE_NAME_ADDR, CurrentProfileName);
  EEPROM.commit(); // save changes to EEPROM
  Serial.println("Settings saved successfully");
}

// Load the settings from EEPROM
void LoadSettings() {
  Serial.println("Loading settings from EEPROM...");
  EEPROM.begin(512); // initialize EEPROM with size 64 bytes

  EEPROM.get(EEPROM_PREHEAT_TEMP_ADDR, preheatTemp);
  EEPROM.get(EEPROM_PREHEAT_TIME_ADDR, preheatTime);
  EEPROM.get(EEPROM_SOAK_TEMP_ADDR, soakTemp);
  EEPROM.get(EEPROM_SOAK_TIME_ADDR, soakTime);
  EEPROM.get(EEPROM_REFLOW_TEMP_ADDR, reflowTemp);
  EEPROM.get(EEPROM_REFLOW_TIME_ADDR, reflowTime);
  EEPROM.get(EEPROM_COOLDOWN_TEMP_ADDR, cooldownTemp);
  EEPROM.get(EEPROM_COOLDOWN_TIME_ADDR, cooldownTime);

  totalTime = preheatTime + soakTime + reflowTime + cooldownTime; // update total time

  EEPROM.get(EEPROM_KP_ADDR, Kp);
  EEPROM.get(EEPROM_KI_ADDR, Ki);
  EEPROM.get(EEPROM_KD_ADDR, Kd);

  myPID.SetTunings(Kp, Ki, Kd);

  CurrentProfileName = GetString(EEPROM_LASTPROFILE_NAME_ADDR);

  Serial.println("Settings loaded successfully");
}

void PutString(int adr, String str){
  uint8_t len = str.length();
  EEPROM.write(adr, len);
  for (int i = 0; i < len; i++){
    EEPROM.write(adr+1+i, str[i]);
  }
}

String GetString(int adr){
  int len = EEPROM.read(adr);
  char data[len+1];

  for (int i=0; i < len; i++){
    data[i] = (char)EEPROM.read(adr+1+i);
  }
  data[len] = '\0';

  return String(data);
}

// This functions mounts LittleFS
void SetupFS() {
    
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  Serial.println("LittleFS Mounted Successfully");
  Serial.println("File System Storage:");
  Serial.print(LittleFS.usedBytes());
  Serial.print(" / ");
  Serial.print(LittleFS.totalBytes());
  Serial.print(" (" + String((float)LittleFS.usedBytes() / (float)LittleFS.totalBytes() * 100, 2) + "%)");
  Serial.println(" bytes used");
}

// This function initializes the Access Point and sets up the web server
void SetupAP() {
  Serial.println("Starting up Access Point...");
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.serveStatic("/static", LittleFS, "/static");
  server.on("/", HTTP_GET, OnConnect);
  server.on("/setvalues", HTTP_POST, SetProfileValues);
  server.on("/setPIDvalues", HTTP_POST, SetPIDValues);
  server.on("/profiles", HTTP_GET, GetProfiles);
  server.on("/saveprofile", HTTP_POST, SaveProfile);
  server.on("/deleteprofile", HTTP_POST, DeleteProfile);
  server.on("/loadprofile", HTTP_POST, LoadProfile);
  server.on("/status", HTTP_GET, GetStatus);

  server.on("/start", HTTP_GET, []() {
    start = true;
    reflowStarted = millis();
    server.send(200, "text/plain", "Reflow process started");
  });

  server.on("/stop", HTTP_GET, []() {
    start = false;
    reflowing = false;
    coolingDown = false;
    soaking = false;
    preheating = false;
    Output = 0; // stop the PID output
    server.send(200, "text/plain", "Reflow process stopped");
  });

  server.onNotFound(NotFound);

  UpdateProfileList();

  server.begin();

  if (!MDNS.begin("tostireflow")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started");
  }
}

// This function sets up the PID controller
void SetupPID(){
  Setpoint = cooldownTemp;
  // tell the PID to range between 0 and the full window size
  myPID.SetOutputLimits(0, 1);

  myPID.SetSampleTime(timeBetweenSamples);

  // turn the PID on
  myPID.SetMode(AUTOMATIC);
  myPID.SetIntegralBounds(-10, 10); // set integral bounds to prevent windup

  pinMode(RELAYPIN, OUTPUT);
  pinMode(STOPBTN, INPUT_PULLUP);
  pinMode(STARTBTN, INPUT_PULLUP);
  
  Output = 0; // initialize Output to 0
}

void SetupDisplay() {
  if (!display.init()){
    Serial.println("SSD1306 display initialization failed!");
    return;
  };
  display.displayOn();
  display.setContrast(255);
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.display();
}

// This function handles the button presses for starting and stopping the reflow process
// No debouncing is required as the boolean flags only allow one press to be registered at a time.
void HandleButtons() {
  if (digitalRead(STOPBTN) == LOW) {
    if (start) {
      Serial.println("Stopping reflow process.");
      reflowing = false;
      coolingDown = false;
      soaking = false;
      preheating = false;
      start = false;
      
      Output = 0; // stop the PID output
    }
  }

  if (digitalRead(STARTBTN) == LOW) {
    if (!start) {
      Serial.println("Starting reflow process.");
      start = true;
      reflowStarted = millis();
    }
  }
}

void HandleDisplay(){
  if (millis() - lastRefresh < refreshTime){
    return;
  }

  lastRefresh = millis();

  if (!start) {
    display.clear();
    display.drawString(0, 0, "Reflow Oven");
    display.drawString(0, 10, "Current Profile:");\
    display.drawString(0, 20, "\"" + CurrentProfileName + "\"");
    display.drawString(0, 30, "Current Temperature: " + String(lastTemperature) + " C");
    display.drawString(0, 40, "Press START to begin");
    display.display();
    return;
  }

  display.clear();
  
  if (preheating) {
    display.drawString(0, 0, "Preheating...");
  } else if (soaking) {
    display.drawString(0, 0, "Soaking...");
  } else if (reflowing) {
    display.drawString(0, 0, "Reflowing...");
  } else if (coolingDown) {
    display.drawString(0, 0, "Cooling Down...");
  }

  display.drawString(0, 10, "Temp: " + String(lastTemperature) + " C");
  display.drawString(0, 20, "Setpoint: " + String(Setpoint) + " C");
  display.drawString(0, 30, "Time Elapsed: " + String(timeSinceReflowStarted / 1000) + " s");
  
  display.display();
}

// This function handles the PID control logic
// It uses the last temperature reading from the thermistor on a set interval
// and adjusts the relay output based on the PID calculations.
void HandlePID(){

  if (!start) return; // do nothing if not started

  timeSinceReflowStarted = millis() - reflowStarted;

  if (timeSinceReflowStarted - lastTimeTempCheck > timeTempCheck){
    lastTimeTempCheck = timeSinceReflowStarted;

    Input = lastTemperature - bias; // read the temperature from the thermistor
    myPID.Compute(); // compute the PID output

    //Serial.println("PIDOutput:" + String(Output) + ",Setpoint:" + String(Setpoint) +",Input: " + String(Input));
    Serial.println(String(lastTemperature) + "," + String(Setpoint) + "," + String((int)(Output * 100)));
  }

  if (timeSinceReflowStarted > totalTime){
    start = false, preheating = false, soaking = false, reflowing = false, coolingDown = false;
    Output = 0;
  }
  else if(timeSinceReflowStarted > (preheatTime + soakTime + reflowTime)){ // preheat and soak and reflow are complete. Start cooldown
    if(!coolingDown){
      preheating = false, soaking = false, reflowing = false, coolingDown = true;
    }
    Setpoint = cooldownTemp;
  }
  else if(timeSinceReflowStarted > (preheatTime + soakTime)){ // preheat and soak are complete. Start reflow
    if(!reflowing){
      preheating = false, soaking = false, reflowing = true, coolingDown = false;
    }
    Setpoint = reflowTemp;
  }
  else if(timeSinceReflowStarted > preheatTime){ // preheat is complete. Start soak
    if(!soaking){
      preheating = false, soaking = true, reflowing = false, coolingDown = false;
    }
    Setpoint = soakTemp;
  }
  else{ // cycle is starting. Start preheat
    if(!preheating){
      preheating = true, soaking = false, reflowing = false, coolingDown = false;
    }
    Setpoint = preheatTemp;
  }
}

void HandleSlowPWM() {
  if (!start) {
    digitalWrite(RELAYPIN, LOW); // ensure relay is off when not started
    return; // do nothing if not started
  }

  // This function is used to control the relay with a slow PWM signal
  if (millis() - lastPeriod > PWM_PERIOD){
    lastPeriod = millis();

    if (Output > 0){
      digitalWrite(RELAYPIN, HIGH); // turn on the relay

      // calculate the duty cycle based on the Output value
      int steps = (int)(Output *  PWM_STEPS); // convert Output to steps 
      unsigned long dutyCycle = steps * dutyCycleStep; // calculate the duty cycle in milliseconds
    }
  }

  if (millis() - lastPeriod > dutyCycleStep) {
    digitalWrite(RELAYPIN, LOW); // turn off the relay
  }
}

// This function handles the thermistor readings and calculates the temperature
void HandleThermistor(){
  if (millis() - lastSampleTime >= timeBetweenSamples) {
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

// This function calculates the temperature based on the average ADC value
// It converts the ADC value to resistance and then calculates the temperature
// using the Steinhart-Hart equation.
void CalculateTemperature(){
  float _res = (float)average;
  if (_res <= 0) _res = 1; // prevent division by zero
  // convert the value to resistance
  _res = ADC_MAX_VALUE / _res - 1;
  if (_res <= 0) _res = 0.0001; // prevent division by zero
  _res = SERIESRESISTOR / _res;
  resistance = _res; // store the resistance value
  
  float steinhart;
  steinhart = resistance / THERMISTORNOMINAL;       // (R/Ro)
  steinhart = log(steinhart);                       // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                        // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                      // Invert
  steinhart -= 273.15;                              // convert absolute temp to C

  if (steinhart < 20.0) steinhart = 20.0;

  lastTemperature = steinhart;
}

// This function updates the list of profiles from the filesystem
void UpdateProfileList(){
  File dir = LittleFS.open(ProfileFolderPrefix, "r");
  if (!dir) {
    Serial.println("Failed to open profile directory");
    return;
  }

  if (!dir.isDirectory()){
    Serial.println("Not a directory");
    server.send(500, "text/plain", "FS failure");
  }

  // read all files in the profile directory and store their names
  int index = 0;
  File file = dir.openNextFile();
  while (file && index < MaxProfiles) {
    ProfileNames[index++] = String(file.name()); // store the file name without extension
    
    file = dir.openNextFile();
  }

  ProfileCount = index; // update the profile count

  // fill remaining slots with empty strings
  for (index; index < MaxProfiles; index++) {
    ProfileNames[index] = ""; 
  }
  
  dir.close();
}

void HandleSerialCommands(){

  if (!Serial.available()) return; // no data available

  String command = Serial.readStringUntil('\n'); // read the command from serial
  command.trim(); // remove any leading/trailing whitespace

  if (command.startsWith("setPID ")) {
    // set PID values from serial command
    int spaceIndex1 = command.indexOf(' ', 7);
    int spaceIndex2 = command.indexOf(' ', spaceIndex1 + 1);

    if (spaceIndex1 == -1 || spaceIndex2 == -1) {
      Serial.println("Invalid command format. Use: setPID <Kp> <Ki> <Kd>");
      return;
    }

    Kp = command.substring(7, spaceIndex1).toFloat();
    Ki = command.substring(spaceIndex1 + 1, spaceIndex2).toFloat();
    Kd = command.substring(spaceIndex2 + 1, command.length()).toFloat();

    myPID.SetTunings(Kp, Ki, Kd); // update PID tunings
    SaveSettings(); // save to EEPROM
    Serial.println("PID values updated: Kp=" + String(Kp, 4) + ", Ki=" + String(Ki, 4) + ", Kd=" + String(Kd, 4));
  }
  
}

//                 ==================================================================
//                 |                     Web Server Handlers                        |
//                 ==================================================================

// ------------- This function serves the main HTML page when the root URL is accessed -------------
void OnConnect(){
  File file = LittleFS.open("/static/index.html", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}
// -------------------------------------------------------------------------------------------------

// ---------------------- This function handles the request to set a profile -----------------------
void NotFound(){
  Serial.println("Not Found: " + server.uri());
  server.send(404, "text/plain", "Not Found");
}
// -------------------------------------------------------------------------------------------------

// ----------------------- This function sets the values from the sent json ------------------------
void SetProfileValues(){
  if (start) {
    server.send(400, "text/plain", "Cannot set values while reflow is in progress");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No data sent");
    return;
  }

  String jsonData = server.arg("plain");
  Serial.println("Received JSON data: " + jsonData);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    Serial.println("Failed to parse JSON: " + String(error.c_str()));
    server.send(400, "text/plain", "Invalid JSON data");
    return;
  }

  // Check if all required fields are present
  if (!doc["preheatTemp"].as<float>() ||
      !doc["preheatTime"].as<unsigned long>() ||
      !doc["soakTemp"].as<float>() ||
      !doc["soakTime"].as<unsigned long>() ||
      !doc["reflowTemp"].as<float>() ||
      !doc["reflowTime"].as<unsigned long>() ||
      !doc["cooldownTemp"].as<float>() ||
      !doc["cooldownTime"].as<unsigned long>()) {
    server.send(400, "text/plain", "Missing required fields");
    return;
  }

  // Set the values from the JSON document
  preheatTemp = doc["preheatTemp"].as<float>();
  preheatTime = doc["preheatTime"].as<unsigned long>() * 1000;
  soakTemp = doc["soakTemp"].as<float>();
  soakTime = doc["soakTime"].as<unsigned long>() * 1000;
  reflowTemp = doc["reflowTemp"].as<float>();
  reflowTime = doc["reflowTime"].as<unsigned long>() * 1000;
  cooldownTemp = doc["cooldownTemp"].as<float>();
  cooldownTime = doc["cooldownTime"].as<unsigned long>() * 1000;
  totalTime = preheatTime + soakTime + reflowTime + cooldownTime;

  CurrentProfileName = "Custom Profile"; // set a default name for the profile
  
  // Save the settings to EEPROM
  SaveSettings();
  Serial.println("Profile values set successfully");

  // Send a success response
  server.send(200, "text/plain", "Profile values set successfully");
  Serial.println("Profile values set: " +
                 String(preheatTemp) + ", " +
                 String(preheatTime) + ", " +
                 String(soakTemp) + ", " +
                 String(soakTime) + ", " +
                 String(reflowTemp) + ", " +
                 String(reflowTime) + ", " +
                 String(cooldownTemp) + ", " +
                 String(cooldownTime));
}
// -------------------------------------------------------------------------------------------------

// --------------------------------- This function sets PID values ---------------------------------
void SetPIDValues(){
  if (start) {
    server.send(400, "text/plain", "Cannot set values while reflow is in progress");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No data sent");
    return;
  }

  String jsonData = server.arg("plain");
  Serial.println("Received JSON data: " + jsonData);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    Serial.println("Failed to parse JSON: " + String(error.c_str()));
    server.send(400, "text/plain", "Invalid JSON data");
    return;
  }

  Kp = doc["kp"].as<float>();
  Ki = doc["ki"].as<float>();
  Kd = doc["kd"].as<float>();

  myPID.SetTunings(Kp, Ki, Kd);
  SaveSettings();
  Serial.println("New PID Settings: Kp= " + String(Kp, 4) + " Ki= " + String(Ki, 4) + " Kd= " + String(Kd, 4));
  server.send(200, "text/plain", "PID values set successfully");
}

// -------------------------------------------------------------------------------------------------

// ------------------ This function returns the list of profiles as a JSON array -------------------
void GetProfiles() {
  UpdateProfileList(); // ensure the profile list is up to date

  String profilesList = "[";
  for (int i = 0; i < MaxProfiles; i++) {
    if (ProfileNames[i].length() > 0) {
      if (i > 0) profilesList += ",";
      profilesList += "\"" + ProfileNames[i] + "\"";
    }
    else {
      break; // stop if we hit an empty slot
    }
  }
  profilesList += "]";

  Serial.println("Profiles List: " + profilesList);

  server.send(200, "application/json", profilesList);
}
// -------------------------------------------------------------------------------------------------

// --------------- This function creates a new profile based on the current settings ---------------
void SaveProfile() {
  if (ProfileCount >= MaxProfiles) {
    server.send(400, "text/plain", "Maximum number of profiles reached");
    return;
  }
  String rawJson = server.arg("plain");
  Serial.println("Save profile data: " + rawJson);
  JsonDocument incoming;
  DeserializationError error = deserializeJson(incoming, rawJson);

  if (error){
    Serial.println("Failed to parse JSON: " + String(error.c_str()));
    server.send(400, "text/plain", "Invalid JSON data");
    return;
  }

  if (!incoming["name"].as<String>()) {
    server.send(400, "text/plain", "Profile name not provided");
    return;
  }

  String profileName = incoming["name"].as<String>();
  if (profileName.length() == 0) {
    server.send(400, "text/plain", "Profile name cannot be empty");
    return;
  }

  // Check if the profile already exists
  if (LittleFS.exists(ProfileFolderPrefix + "/" + profileName)) {
    server.send(400, "text/plain", "Profile already exists");
    return;
  }

  // Create a new file for the profile
  File file = LittleFS.open(ProfileFolderPrefix + "/" + profileName, "w", true);
  if (!file) {
    server.send(500, "text/plain", "Failed to create profile");
    return;
  }

  // Create a JSON document to store the profile data
  JsonDocument doc;
  doc["preheatTemp"] = preheatTemp;
  doc["preheatTime"] = preheatTime;
  doc["soakTemp"] = soakTemp;
  doc["soakTime"] = soakTime;
  doc["reflowTemp"] = reflowTemp;
  doc["reflowTime"] = reflowTime;
  doc["cooldownTemp"] = cooldownTemp;
  doc["cooldownTime"] = cooldownTime;

  // Serialize the JSON document to the file
  String jsonString;
  serializeJson(doc, jsonString);

  if (file.print(jsonString)) {
    file.close();
    Serial.println("Profile created: " + profileName);
    
    // Update the profile list after creation
    UpdateProfileList();

    // Save the current profile name to EEPROM
    CurrentProfileName = profileName;
    PutString(EEPROM_LASTPROFILE_NAME_ADDR, CurrentProfileName);
    EEPROM.commit(); // save changes to EEPROM
    
    server.send(200, "text/plain", "Profile created successfully");
  } else {
    file.close();
    LittleFS.remove(ProfileFolderPrefix + "\\" + profileName); // clean up if write failed
    server.send(500, "text/plain", "Failed to write profile data");
  }

}
// -------------------------------------------------------------------------------------------------

// ------------------ This function deletes a profile based on the provided name -------------------
void DeleteProfile() {

  String rawJson = server.arg("plain");
  Serial.println("Save profile data: " + rawJson);
  JsonDocument incoming;
  DeserializationError error = deserializeJson(incoming, rawJson);

  if (error){
    Serial.println("Failed to parse JSON: " + String(error.c_str()));
    server.send(400, "text/plain", "Invalid JSON data");
    return;
  }

  if (!incoming["name"].as<String>()) {
    server.send(400, "text/plain", "Profile name not provided");
    return;
  }

  String profileName = incoming["name"].as<String>();
  if (profileName.length() == 0) {
    server.send(400, "text/plain", "Profile name cannot be empty");
    return;
  }

  // Check if the profile already exists
  if (!LittleFS.exists(ProfileFolderPrefix + "/" + profileName)) {
    server.send(400, "text/plain", "Profile does not exists");
    return;
  }

  if (LittleFS.remove(ProfileFolderPrefix + "/" + profileName)) {
    // Update the profile list after deletion
    UpdateProfileList();
    Serial.println("Profile deleted: " + profileName);
    server.send(200, "text/plain", "Profile deleted successfully");
  } else {
    server.send(404, "text/plain", "Profile not found");
  }
}
// -------------------------------------------------------------------------------------------------

// --------------- This function sets the current profile based on the provided name ---------------
void LoadProfile(){
  if (start) {
    server.send(400, "text/plain", "Cannot load profile while reflow is in progress");
    return;
  }

  String rawJson = server.arg("plain");
  Serial.println("Save profile data: " + rawJson);
  JsonDocument incoming;
  DeserializationError error = deserializeJson(incoming, rawJson);

  if (error){
    Serial.println("Failed to parse JSON: " + String(error.c_str()));
    server.send(400, "text/plain", "Invalid JSON data");
    return;
  }

  if (!incoming["name"].as<String>()) {
    server.send(400, "text/plain", "Profile name not provided");
    return;
  }

  String profileName = incoming["name"].as<String>();
  if (profileName.length() == 0) {
    server.send(400, "text/plain", "Profile name cannot be empty");
    return;
  }

  // Check if the profile exists
  if (!LittleFS.exists(ProfileFolderPrefix + "/" + profileName)) {
    server.send(400, "text/plain", "Profile does not exist");
    return;
  }

  File file = LittleFS.open(ProfileFolderPrefix + "/" + profileName, "r");

  JsonDocument doc;
  error = deserializeJson(doc, file);

  if (error) {
    Serial.println("Failed to parse profile: " + String(error.c_str()));
    server.send(500, "text/plain", "Failed to parse profile");
    file.close();
    return;
  }

  preheatTemp = doc["preheatTemp"] | 100.0;
  preheatTime = doc["preheatTime"] | 120000; // default 2 minutes
  soakTemp = doc["soakTemp"] | 150.0;
  soakTime = doc["soakTime"] | 60000; // default 1 minute
  reflowTemp = doc["reflowTemp"] | 230.0;
  reflowTime = doc["reflowTime"] | 120000; // default 2 minutes
  cooldownTemp = doc["cooldownTemp"] | 25.0;
  cooldownTime = doc["cooldownTime"] | 120000; // default 2 minutes
  CurrentProfileName = profileName; // set the current profile name

  totalTime = preheatTime + soakTime + reflowTime + cooldownTime;
  
  file.close();

  // Save the loaded settings to EEPROM
  SaveSettings();
  Serial.println("Profile loaded: " + profileName);
  
  server.send(200, "text/plain", "Profile set successfully");
}
// -------------------------------------------------------------------------------------------------

// ---------------- This function returns the current status of the reflow process -----------------
void GetStatus() {

  int elapsedTimeInSeconds = (int)((millis() - reflowStarted) / 1000); // elapsed time in seconds
  int totalTimeInSeconds = (int)(totalTime / 1000); // total time in seconds

  JsonDocument doc;
  
  doc["preheating"] = preheating;
  doc["soaking"] = soaking;
  doc["reflowing"] = reflowing;
  doc["coolingDown"] = coolingDown;
  doc["start"] = start;
  doc["lastTemperature"] = lastTemperature;
  doc["resistance"] = resistance;
  doc["preheatTemp"] = preheatTemp;
  doc["preheatTime"] = preheatTime / 1000; // convert to seconds
  doc["soakTemp"] = soakTemp;
  doc["soakTime"] = soakTime / 1000; // convert to seconds
  doc["reflowTemp"] = reflowTemp;
  doc["reflowTime"] = reflowTime / 1000; // convert to seconds
  doc["cooldownTemp"] = cooldownTemp;
  doc["cooldownTime"] = cooldownTime / 1000; // convert to seconds
  doc["totalTime"] = totalTime / 1000; // convert to seconds
  doc["kp"] = Kp;
  doc["ki"] = Ki;
  doc["kd"] = Kd;
  doc["currentProfile"] = CurrentProfileName;

  if (start){
    doc["time"] = String(elapsedTimeInSeconds) + "/" + String(totalTimeInSeconds) + " seconds";
  }
  else {
    doc["time"] = "Idle";
  }
  doc["setpoint"] = Setpoint;
  doc["pidOutput"] = Output;

  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}
// -------------------------------------------------------------------------------------------------

