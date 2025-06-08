#include <Arduino.h>
#include <Esp.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PID_v1.h>
#include <EEPROM.h>
#include "LittleFS.h"
#include <ArduinoJson.h>

// ---------------- Stored Profiles and Settings EEPROM adresses ----------------

const int MaxProfiles = 20; // maximum number of profiles
int ProfileCount = 0; // current number of profiles
String ProfileFolderPrefix = "/profiles"; // folder prefix for profiles
String ProfileNames[MaxProfiles]; // array to store profile names

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
#define NUMSAMPLES 5
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

unsigned long timeTempCheck = 200; 
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

double Kp=2, Ki=5, Kd=1;

PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);


// ---------------- Function prototypes ----------------
void SetupFS();
void SetupAP();
void SetupPID();

void HandleButtons();
void HandlePID();
void HandleThermistor();
void CalculateTemperature();
void UpdateProfileList();

void OnConnect();
void GetProfiles();
void CreateProfile();
void DeleteProfile();
void SetProfile();
void GetStatus();
void NotFound();


// --------------- Setup and Loop ----------------
void setup() {
  Serial.begin(115200);
  SetupFS();
  SetupAP();
  SetupPID();
}

void loop() {
  server.handleClient(); // handle incoming client requests
  //HandleButtons();
  HandlePID();
  HandleThermistor();
}

// ===================================================================
// |                     Function Definitions                        |
// ===================================================================

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

void SetupAP() {
  Serial.println("Starting up Access Point...");
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.serveStatic("/static", LittleFS, "/static");
  server.on("/", HTTP_GET, OnConnect);
  server.on("/profiles", HTTP_GET, GetProfiles);
  server.on("/createprofile", HTTP_POST, CreateProfile);
  server.on("/deleteprofile", HTTP_DELETE, DeleteProfile);
  server.on("/setprofile", HTTP_PUT, SetProfile);
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
    digitalWrite(RELAYPIN, LOW);
    server.send(200, "text/plain", "Reflow process stopped");
  });

  server.onNotFound(NotFound);

  UpdateProfileList();

  server.begin();

}

void SetupPID(){
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
      digitalWrite(RELAYPIN, 0);
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

// This function handles the PID control logic
// It uses the last temperature reading from the thermistor on a set interval
// and adjusts the relay output based on the PID calculations.
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

  lastTemperature = steinhart;
}

// This function updates the list of profiles from the filesystem
void UpdateProfileList(){
  File dir = LittleFS.open(ProfileFolderPrefix, "r", true);
  if (!dir) {
    Serial.println("Failed to open profile directory");
    return;
  }

  // read all files in the profile directory and store their names
  int index = 0;
  File file = dir.openNextFile();
  while (file) {
    if (index < MaxProfiles) {
      ProfileNames[index++] = String(file.name()); // store the file name without extension
    }
    file = dir.openNextFile();
  }

  ProfileCount = index; // update the profile count

  // fill remaining slots with empty strings
  for (index; index < MaxProfiles; index++) {
    ProfileNames[index] = ""; 
  }
  
  dir.close();
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

// ------------------ This function returns the list of profiles as a JSON array -------------------
void GetProfiles() {
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

// --------- This function creates a new profile based on the provided name and JSON data ----------
void CreateProfile() {
  if (ProfileCount >= MaxProfiles) {
    server.send(400, "text/plain", "Maximum number of profiles reached");
    return;
  }

  if (!server.hasArg("profile")) {
    server.send(400, "text/plain", "Profile name not provided");
    return;
  }

  String profileName = server.arg("profile");
  if (profileName.length() == 0) {
    server.send(400, "text/plain", "Profile name cannot be empty");
    return;
  }

  // Check if the profile already exists
  File existingFile = LittleFS.open(ProfileFolderPrefix + "\\" + profileName, "r");
  if (existingFile) {
    existingFile.close();
    server.send(400, "text/plain", "Profile already exists");
    return;
  }

  // Check if the json in the body is valid
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Profile data not provided");
    return;
  }

  String profileData = server.arg("plain");
  JsonDocument doc; // Create a JSON document with a capacity of 1024 bytes
  DeserializationError error = deserializeJson(doc, profileData);
  if (error) {
    Serial.println("Failed to parse profile data: " + String(error.c_str()));
    server.send(400, "text/plain", "Invalid profile data");
    return;
  }
  // Validate required fields
  if (!doc["preheatTemp"].is<float>() || !doc["preheatTime"].is<unsigned long>() ||
      !doc["soakTemp"].is<float>() || !doc["soakTime"].is<unsigned long>() ||
      !doc["reflowTemp"].is<float>() || !doc["reflowTime"].is<unsigned long>() ||
      !doc["cooldownTemp"].is<float>() || !doc["cooldownTime"].is<unsigned long>()) {
    server.send(400, "text/plain", "Missing required profile fields");
    return;
  }

  // check if the profile fits within constraints
  if (doc["preheatTemp"].as<float>() < 0 || doc["preheatTemp"].as<float>() > 300 ||
      doc["soakTemp"].as<float>() < 0 || doc["soakTemp"].as<float>() > 300 ||
      doc["reflowTemp"].as<float>() < 0 || doc["reflowTemp"].as<float>() > 300 ||
      doc["cooldownTemp"].as<float>() < 0 || doc["cooldownTemp"].as<float>() > 300 ||

      doc["preheatTime"].as<unsigned long>() < 0 || doc["soakTime"].as<unsigned long>() < 0 ||
      doc["reflowTime"].as<unsigned long>() < 0 || doc["cooldownTime"].as<unsigned long>() < 0) {
    server.send(400, "text/plain", "Profile values out of range");
    return;
  }

  File file = LittleFS.open(ProfileFolderPrefix + "\\" + profileName, "w", true);
  if (!file) {
    server.send(500, "text/plain", "Failed to create profile");
    return;
  }
  
  // Serialize the JSON document to the file
  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write profile data to file");
    server.send(500, "text/plain", "Failed to write profile data");
    file.close();
    LittleFS.remove(ProfileFolderPrefix + "\\" + profileName); // clean up if write failed
    return;
  }

  file.close();
  server.send(200, "text/plain", "Profile created successfully");
}
// -------------------------------------------------------------------------------------------------

// ------------------ This function deletes a profile based on the provided name -------------------
void DeleteProfile() {
  if (!server.hasArg("profile")) {
    server.send(400, "text/plain", "Profile name not provided");
    return;
  }

  String profileName = server.arg("profile");
  if (LittleFS.remove(ProfileFolderPrefix + "\\" + profileName)) {
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
void SetProfile(){
  if (!server.hasArg("profile")) {
    server.send(400, "text/plain", "Profile name not provided");
    return;
  }

  String profileName = server.arg("profile");
  File file = LittleFS.open(ProfileFolderPrefix + "\\" +profileName, "r");

  if (!file) {
    server.send(404, "text/plain", "Profile not found");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);

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

  totalTime = preheatTime + soakTime + reflowTime + cooldownTime;
  
  file.close();
  
  server.send(200, "text/plain", "Profile set successfully");
}
// -------------------------------------------------------------------------------------------------

// ---------------- This function returns the current status of the reflow process -----------------
void GetStatus() {

  int elapsedTimeInSeconds = (int)((millis() - reflowStarted) / 1000); // elapsed time in seconds, rounded to 2 decimal places
  int totalTimeInSeconds = (int)(totalTime / 1000); // total time in seconds

  JsonDocument doc;
  
  doc["preheating"] = preheating;
  doc["soaking"] = soaking;
  doc["reflowing"] = reflowing;
  doc["coolingDown"] = coolingDown;
  doc["start"] = start;
  doc["lastTemperature"] = lastTemperature;
  doc["resistance"] = resistance;

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

