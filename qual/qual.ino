#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <utility>
#include "ADE7758.h"
#include <FS.h>
#include <ArduinoJson.h>
#include "Time.h"

#define DIV 2
#define ADE_SPI_CS 16
#define SD_SPI_CS 4
#define RELAY_CONTROL 5
#define MEAS_AMOUNT 50

ESP8266WebServer server(80);
struct tm *timeinfo;
ADE7758 meter(ADE_SPI_CS);
SPISettings SD_Settings(10000000, MSBFIRST, SPI_MODE0);
SPISettings ADE_Settings(8000000, MSBFIRST, SPI_MODE2);

struct Config {
  String netSSid;
  String netPassword;
  String _localIP;
  String APssid;
  String APpass;
  String AP_localIP;
  String serverName;
  bool sendToServ;
  bool writeSD; 
  bool stopMeasurements;
  bool syncMeasurements;
  unsigned long Vlimit;
  unsigned long Ilimit;
} _config;

struct State {
  bool light;
  bool SDcard;  
  unsigned long unSyncFilesAmount;
} _state;

bool stopMeas = 0, localMode = 0, fileReady = 0, synchronized = 0, voltageExceeded = 0, currencyExceeded = 0, voltageLowerExceeded = 0;
unsigned long lastUpdatedTime = 0, lastUpdateTime = 0, measTimer = 0, exceededTime = 0, syncTimer = 0;
unsigned long measAmountSD = 0, lastFileName = 0;
String resultStr = "";
uint8_t measureCounter = 0;
uint16_t offset = 0;
String folderName = "UnSyncMe";
std::pair<bool, String> resultSync;
std::pair<unsigned long, unsigned long> resultCount;

unsigned long Voltage_a,Voltage_b,Voltage_c;
unsigned long Currency_a,Currency_b,Currency_c;
unsigned long calculatedV;
float calculatedI;

std::pair<bool, String> readSDfile(String path) {
  std::pair<bool, String> result;
  
  SPI.beginTransaction(SD_Settings);
  if (!SD.exists(path)) {
    result.first = false;
    result.second =  "Error: File " + path + " not found!";
    return result;
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    result.first = false;
    result.second = "Error: Failed to open " + path;
    return result;
  } else {
    while (file.available()){
      result.second  += char(file.read());
    }
  }
  file.close();
  SPI.endTransaction();
  result.first = true;
  return result;
}

std::pair<bool, String> deleteSDfile(String path){
  std::pair<bool, String> result;
  SPI.beginTransaction(SD_Settings);
    if (SD.exists(path)) {
      if (SD.remove(path)) {
        SPI.endTransaction();
        measAmountSD--;
        _state.unSyncFilesAmount = measAmountSD;
        result.first = true;
        result.second = "File successfully deleted";
        return result;
        } else {
          SPI.endTransaction();
          result.first = false;
          result.second = "Error while deleting " + path + " file";
          return result;
        }
    } else {
      SPI.endTransaction();
      result.first = false;
      result.second = "File " + path + " doesnt exist";
      return result;
    }
}

std::pair<bool, String> getSDfileName(String folder){
  std::pair<bool, String> result;
  
  SPI.beginTransaction(SD_Settings);
  File root = SD.open(folder);
  yield();
  if (!root.isDirectory()) {
    SPI.endTransaction();
    root.close();
    result.first = false;
    result.second = "Folder " + folder + " not found";
    return result;
  }

  File file;
  uint16_t fileIndex = 0;

  while (fileIndex <= offset) {
    file = root.openNextFile();
    if (!file) {
      SPI.endTransaction();
      file.close();
      root.close();
      result.first = false;
      result.second = "Error opening file" + folder + " " + String(offset, DEC);
      return result;
    }
    fileIndex++;
  }

  result.first = true;
  result.second = file.name();

  file.close();
  root.close();
  SPI.endTransaction();
  
  return result;
}

std::pair<unsigned long, unsigned long> countMeasAmountSD(String folder){
  std::pair<unsigned long, unsigned long> result;
  result.first = 0;
  result.second = 0;
  String lastFileName = "", number = "";
  unsigned long largestNumber = 0;
  
  SPI.beginTransaction(SD_Settings);
  File root = SD.open(folder);
  yield();
  if (!root.isDirectory()){
    Serial.println("Error: " + folder + " directory not found");
    root.close();
    root = SD.open("/");
    if (!SD.mkdir(folder)){
      yield();
      Serial.println("Error creating folder " + folder);
    } else {
      Serial.println("creating folder " + folder);
    }
    root.close();
    SPI.endTransaction();
    return result;
  }
  File file = root.openNextFile();
  yield();
  if (!file){
    Serial.println("Error: No files found in " + folder +  " directory");
    SPI.endTransaction();
    return result;
  }
  while (file) {
    if (!file.isDirectory()) {
      result.first++;
      lastFileName = file.name();
      number = lastFileName.substring(0, lastFileName.length() - 4);
      if (number.toInt() > largestNumber){
        largestNumber = number.toInt();
      }
      yield();
    }
    file = root.openNextFile();
    yield();
  }
  file.close();
  root.close();
  SPI.endTransaction();
  yield();
  Serial.print("Кількість файлів у папці " + folder + " :");
  Serial.println(result.first);
  result.second = largestNumber;

  Serial.print("Найбільше ім'я файлу: ");
  Serial.println(result.second);
  return result;
}

std::pair<bool, String> syncFile(String folder){
  std::pair<bool, String> resultRead, resultDelete, resultName, resultSend;

  resultName = getSDfileName(folderName);
  if (!resultName.first) {
    return resultName;
  }

  String syncPath = folderName + "/" + resultName.second;
  resultRead = readSDfile(syncPath);
  if (!resultRead.first) {
    return resultRead;
  }

  if (!sendMeasurements(resultRead.second)) {
    resultSend.first = false;
    resultSend.second = "Error sending file " + syncPath;
    offset++;
    return resultSend;
  } 

  resultDelete = deleteSDfile(syncPath);

  if (!resultDelete.first) {
    offset++;
    return resultDelete;
  }

  return resultDelete;
}

void handleRoot(){
  stopMeas = true;
  handleFile("/index.html");
}

void handleMeas(){
  if(server.arg("state").toInt() == 1){
    stopMeas = true;
    server.send(200, "text/plain", "Measurement on pause");
  } else {
    stopMeas = false;
    server.send(200, "text/plain", "Measurements continued");
  }
}
void handleLight(){
  if(server.arg("state").toInt() == 1){
    if (voltageExceeded || currencyExceeded) {
      server.send(200, "text/plain", "Light cannot be turned on, voltage: " + String(voltageExceeded) + "or current: " + String(currencyExceeded) + "exceeded. Try again later");
    } else {
      digitalWrite(RELAY_CONTROL, HIGH);
      _state.light = 1;
      server.send(200, "text/plain", "ON");
    }
  } else {
    digitalWrite(RELAY_CONTROL, LOW);
    _state.light = 0;
    server.send(200, "text/plain", "OFF");
  }
}

void handleChangeSettings(){
  if (server.hasArg("plain")) {
    String jsonData = server.arg("plain");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, jsonData);

    if(error.code() != DeserializationError::Ok){
      server.send(500, "text/plain", error.c_str());
      return;
    }
    _config.netSSid = doc["netSSid"].as<String>();
    _config.netPassword = doc["netPassword"].as<String>();
    _config._localIP = doc["localIP"].as<String>();
    _config.APssid = doc["APssid"].as<String>();
    _config.APpass = doc["APpass"].as<String>();
    _config.AP_localIP = doc["APlocalIP"].as<String>();
    _config.sendToServ = doc["sendToServ"].as<bool>();
    _config.writeSD = doc["writeSD"].as<bool>();
    _config.stopMeasurements = doc["stopMeasurements"].as<bool>();
    _config.syncMeasurements = doc["syncMeasurements"].as<bool>();
    _config.serverName = doc["serverName"].as<String>();
    _config.Vlimit = doc["Vlimit"].as<unsigned long>();
    _config.Ilimit = doc["Ilimit"].as<unsigned long>();
    writeConfig(&_config);
    server.send(200, "text/plain", "Settings received successfully");
  } else {
    server.send(400, "text/plain", "Error: No data received");
  }

}

void handleFile(char *filename) {
  File f = SPIFFS.open(filename, "r");
    if (f){
      String s;
      while (f.available()){
        s += char(f.read());
      }
      String contentType = getContentType(filename);
      server.send(200, contentType, s);
    }
  else {
      server.send(200, "text/html", "Error: File does not exist");
  }
}

void handleCSS() {
  handleFile("/style.css");
}

void handleConfig(){
  handleFile("/config.json");
}

void handleState(){
  server.send(200, "text/plain", createStateString(&_state));
}

void handleFilesList(){
  if (!server.hasArg("from") || !server.hasArg("to") || !server.hasArg("folder")) {
    server.send(400, "text/plain", "Missing 'from', 'to' or 'path' parameter");
    return;
  }
  uint16_t _from = 0, _to = 0;
  String folder = "";
  _from = server.arg("from").toInt();
  _to = server.arg("to").toInt();
  folder = server.arg("folder");
  server.send(200, "text/plain", createFilesList(_from, _to, folder));
}

void handleGetSDfile(){
  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing 'path' parameter");
    return;
  }
  String path = server.arg("path");

  std::pair<bool, String> result = readSDfile(path);

  if (result.first) {
    server.send(200, "text/plain", result.second);
  } else {
    server.send(400, "text/plain", result.second);
  }
  
}

void handleDeleteSDfile() {
  if (server.hasArg("path")) {
    String path = server.arg("path");
    std::pair<bool, String> result = deleteSDfile(path);
    
    if (result.first){
      server.send(200, "text/plain", result.second);
    } else {
      server.send(200, "text/plain", result.second);
    }
  } else {
    server.send(400, "text/plain", "Missing path parameter");
  }
}

void handleUpdateTime(){
  if (!updateTime()){
    server.send(400, "text/plain", "Cannot update time!!!!");
  } else {
    server.send(200, "text/plain", "Time updated!!!!");
  }
}

void handleScript() {
  handleFile("/script.js");
}

void handleLibrary() {
  File dataFile = SPIFFS.open("/jquery.min.js", "r");
  if(server.streamFile(dataFile,"application/javascript") != dataFile.size()) {
    Serial.println("Did not stream entire file, maybe file path not correct!");
  }
  dataFile.close();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ADE_SPI_CS, OUTPUT);
  pinMode(SD_SPI_CS, OUTPUT);
  pinMode(RELAY_CONTROL, OUTPUT);

  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(ADE_SPI_CS, HIGH);
  digitalWrite(RELAY_CONTROL, LOW);
  
  Serial.begin(115200);
  delay(300);
  SPI.begin();
  delay(300);

  if (!SPIFFS.begin()) {
    Serial.println("Файлова система не ініціалізована");
  }
  delay(500);
  readConfig(&_config);
  _state.light = 0;

  Serial.print("Connecting to ");
  Serial.println(_config.netSSid);

  WiFi.begin(_config.netSSid, _config.netPassword);
  uint8_t netCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    netCount++;
    delay(500);
    Serial.print(".");
    if (netCount > 10){
      WiFi.disconnect();
      localMode = 1;
      break;
    }
  }

  if (!localMode){
    Serial.println("IP адреса: ");
    Serial.println(WiFi.localIP());
  }

  WiFi.softAP(_config.APssid, _config.APpass);
  Serial.print("AP IP адреса: ");
  Serial.println(WiFi.softAPIP());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/light", handleLight);
  server.on("/settings", handleChangeSettings);
  server.on("/filesList", handleFilesList);
  server.on("/getSDfile", handleGetSDfile);
  server.on("/deleteSDfile", handleDeleteSDfile);
  server.on("/meas", handleMeas);
  server.on("/updateTime", handleUpdateTime);
  server.on("/style.css", handleCSS);
  server.on("/script.js", handleScript);
  server.on("/jquery.min.js", handleLibrary);
  server.on("/config.json", handleConfig);
  server.on("/state.json", handleState);
  server.begin();

  if (!updateTime()){
    Serial.println("Cannot update time!!!!");
  } else {
    Serial.println("Update time!!!!");
  }
  Serial.println(lastUpdatedTime);

  SPI.beginTransaction(SD_Settings);
  if (!SD.begin(SD_SPI_CS)) {
    Serial.println(F("Помилка SD карти"));
    _state.SDcard = 0;
    yield();
    SPI.endTransaction();
    digitalWrite(SD_SPI_CS, HIGH);
  } else {
    SPI.endTransaction();
    Serial.println(F("SD карта ініціалізована"));
    _state.SDcard = 1;
    yield();
    resultCount = countMeasAmountSD("UnSyncMe");
    measAmountSD = resultCount.first;
    lastFileName = resultCount.second;
    _state.unSyncFilesAmount = measAmountSD;
    SdFile::dateTimeCallback(cb_dateTime);
  }
  SPI.beginTransaction(ADE_Settings);
  meter.setLcycMode(0);
  meter.gainSetup(INTEGRATOR_OFF,FULLSCALESELECT_0_5V,GAIN_1,GAIN_1);
  meter.setupDivs(DIV,DIV,DIV);
  Serial.println(meter.resetStatus(), BIN); 
  SPI.endTransaction();
  Serial.println("Configured ADE7758");
}

void loop() {
  
  server.handleClient();
  if (!stopMeas && !_config.stopMeasurements){
    if (millis() - measTimer > 10){
      if (!fileReady){
        createNewResultFile();
        fileReady = true;
      }
      measureCounter++;
      SPI.beginTransaction(ADE_Settings);
      Voltage_a = meter.avrms();
      if (_state.light){
        calculatedV = Voltage_a/4672;
        if (calculatedV > _config.Vlimit) {
          digitalWrite(RELAY_CONTROL, LOW);
          voltageExceeded = true;
          exceededTime = millis();
        } 

        if (calculatedV < 50) {
          digitalWrite(RELAY_CONTROL, LOW);
          voltageLowerExceeded = true;
          exceededTime = millis();
        }
      }
      Voltage_b = meter.bvrms();
      Voltage_c = meter.cvrms();
      Currency_a = meter.airms();
      if (_state.light){
        calculatedI = Currency_a/1914753;
        calculatedI = calculatedI * 100;
        if (calculatedI > _config.Ilimit) {
          digitalWrite(RELAY_CONTROL, LOW);
          currencyExceeded = true;
          exceededTime = millis();
        }
      }
      Currency_b = meter.birms();
      Currency_c = meter.cirms();
      SPI.endTransaction();
      addMeasurement(Voltage_a, Voltage_b, Voltage_c, Currency_a, Currency_b, Currency_c);
  
      if (measureCounter == MEAS_AMOUNT) {
        Serial.println("end of write");
        measureCounter = 0;
        fileReady = 0;
        endFile();
  
        if (_config.sendToServ){
          if (sendMeasurements(resultStr)){
            synchronized = true;
          } else {
            synchronized = false;
          }
        } else {
          synchronized = false;
        }
  
        if (_config.writeSD){
          if (!synchronized){
            if (!saveMeasOnSD(folderName)){
              _state.SDcard = 0;
            } else {
              _state.SDcard = 1;
              _state.unSyncFilesAmount = measAmountSD;
            }
          }
        }
      }
      
      measTimer = millis();
      
      if (voltageLowerExceeded && (millis() - exceededTime >= 15000)) {
        voltageLowerExceeded = false;
      }

      if (voltageExceeded && (millis() - exceededTime >= 15000)) {
        voltageExceeded = false;
      }

      if (currencyExceeded && (millis() - exceededTime >= 15000)) {
        currencyExceeded = false;
      }
      
      
      if (!voltageLowerExceeded && !voltageExceeded && !currencyExceeded && _state.light) {
        digitalWrite(RELAY_CONTROL, HIGH);
      }
    }
  }
  
  if (_config.syncMeasurements){
    if ((measAmountSD > offset) && synchronized && _state.SDcard){
      if (millis() - syncTimer > 10000){
        resultSync = syncFile(folderName);
        Serial.println(resultSync.second);
        syncTimer = millis();
      }
    }
  }
  
  if ((millis() - lastUpdateTime > 1800000) && synchronized){
    updateTime();
    resultCount = countMeasAmountSD("UnSyncMe");
    measAmountSD = resultCount.first;
    lastFileName = resultCount.second;
    _state.unSyncFilesAmount = measAmountSD;
    offset = 0;
  }
 
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool sendMeasurements(String s) {
  HTTPClient http; 
  WiFiClient client;
  
  http.begin(client, _config.serverName + "/measurements");
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(s);

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    http.end();
    return true;
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
  }
}

bool updateTime(){
  HTTPClient http; 
  WiFiClient client;
  String timeName = _config.serverName + "/time";
  http.begin(client, timeName);
  int httpResponseCode = http.GET();

  int maxRetries = 3;  
  int retryCount = 0;   

  while (retryCount < maxRetries && httpResponseCode <= 0) {
    delay(500);
    httpResponseCode = http.GET();
    retryCount++;
  }
  if (httpResponseCode > 0){
    String response = http.getString();
    DynamicJsonDocument doc(100);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.println("Error: Failed to parse JSON");
      return 0;
    }
    lastUpdatedTime = doc["current_time"].as<unsigned long>();
    lastUpdateTime = millis();
    http.end();
    return 1;
  } else {
    http.end();
    return 0;
  }
}

void cb_dateTime(uint16_t* _date, uint16_t* _time) {
  time_t unixTime = lastUpdatedTime + ((millis() - lastUpdateTime)/1000);
  timeinfo = localtime(&unixTime);
  *_date = FAT_DATE(timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  *_time = FAT_TIME(timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  Serial.println("Hellu!");
}

void readConfig(Config *_config){
  File configFile;
   if (!SPIFFS.exists("/config.json")) {
    configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Failed to create config file");
      return;
    }
    DynamicJsonDocument doc(1024);
    doc["netSSid"] = "admin";
    doc["netPassword"] = "admin123";
    doc["localIP"] = "192.168.1.2";
    doc["sendToServ"] = 1;
    doc["writeSD"] = 1;
    doc["stopMeasurements"] = 0;
    doc["syncMeasurements"] = 0;
    doc["APssid"] = "ESP";
    doc["APpass"] = "";
    doc["APlocalIP"] = "192.168.4.1";
    doc["serverName"] = "http://192.168.1.5:5000";
    doc["Vlimit"] = 280;
    doc["Ilimit"] = 50;
    
    if (serializeJson(doc, configFile) == 0) {
      Serial.println("Failed to write to file");
    }
    configFile.close();
  }

  configFile = SPIFFS.open("/config.json", "r");
  size_t size = configFile.size();
  char* buf = new char[size];
  configFile.readBytes(buf, size);
  DynamicJsonDocument doc(1024);
  auto error = deserializeJson(doc, buf);
  if (error) {
    Serial.println("Failed to parse config file");
    return;
  }
  _config->netSSid = doc["netSSid"].as<String>();
  _config->netPassword = doc["netPassword"].as<String>();
  _config->_localIP = doc["localIP"].as<String>();
  _config->APssid = doc["APssid"].as<String>();
  _config->APpass = doc["APpass"].as<String>();
  _config->AP_localIP = doc["APlocalIP"].as<String>();
  _config->sendToServ = doc["sendToServ"].as<bool>();
  _config->writeSD = doc["writeSD"].as<bool>();
  _config->stopMeasurements = doc["stopMeasurements"].as<bool>();
  _config->syncMeasurements = doc["syncMeasurements"].as<bool>();
  _config->serverName = doc["serverName"].as<String>();
  _config->Vlimit = doc["Vlimit"].as<unsigned long>();
  _config->Ilimit = doc["Ilimit"].as<unsigned long>();
  delete[] buf;
  configFile.close();
}

void writeConfig(Config *_config){
  File configFile;
  configFile = SPIFFS.open("/config.json", "w");
  DynamicJsonDocument doc(1024);
  doc["netSSid"] = _config->netSSid;
  doc["netPassword"] = _config->netPassword;
  doc["localIP"] = _config->_localIP;
  doc["sendToServ"] = _config->sendToServ;
  doc["stopMeasurements"] = _config->stopMeasurements;
  doc["syncMeasurements"] = _config->syncMeasurements;
  doc["writeSD"] = _config->writeSD;
  doc["APssid"] = _config->APssid;
  doc["APpass"] = _config->APpass;
  doc["APlocalIP"] = _config->AP_localIP;
  doc["serverName"] = _config->serverName;
  doc["Vlimit"] = _config->Vlimit;
  doc["Ilimit"] = _config->Ilimit;
  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write to file");
  }
  configFile.close();
}

void createNewResultFile(){
  resultStr = "";
  resultStr += "{\n";
  resultStr += "\"data\":[\n";
}

void addMeasurement(unsigned long phaseVA, unsigned long phaseVB, unsigned long phaseVC, unsigned long phaseIA,unsigned long phaseIB, unsigned long phaseIC ) {
  String measurement = "";
  unsigned long unixTime = lastUpdatedTime + ((millis() - lastUpdateTime)/1000);
  measurement += "{\n";
  measurement += "\"Time\": ";
  measurement += unixTime;
  measurement += ",\n";
  measurement += "\"PhaseVA\": ";
  measurement += phaseVA;
  measurement += ",\n";
  measurement += "\"PhaseVB\": ";
  measurement += phaseVB;
  measurement += ",\n";
  measurement += "\"PhaseVC\": ";
  measurement += phaseVC;
  measurement += ",\n";
  measurement += "\"PhaseIA\": ";
  measurement += phaseIA;
  measurement += ",\n";
  measurement += "\"PhaseIB\": ";
  measurement += phaseIB;
  measurement += ",\n";
  measurement += "\"PhaseIC\": ";
  measurement += phaseIC;
  measurement += "},\n";
  resultStr += measurement;
}

void endFile(){
  if (!resultStr.isEmpty()) {
    resultStr.remove(resultStr.length() - 2, 2);
  }
  resultStr += "]\n}";
}

bool saveMeasOnSD(String folder){
  int resultSize = 0;
  resultSize = resultStr.length();
  if (resultSize < 5) {
    Serial.println("Рядок з результатами вимірювань сформовано неправильно: " + String(resultSize, DEC));
    return false;
  } else {
    Serial.println("Розмір рядка: " +  String(resultSize, DEC));
  }
  
  String fileName = folder + "/" + String(lastFileName + 1, DEC) + ".txt";
  SPI.beginTransaction(SD_Settings);
  delay(5);

  File dataFile = SD.open(fileName, FILE_WRITE);

  if (dataFile) {
    uint16_t bytesWritten = dataFile.println(resultStr);
    if (bytesWritten < resultSize) {
      Serial.println("Помилка запису файлу " + fileName + "записано байт: " + bytesWritten);
      dataFile.close();
      SPI.endTransaction();
      return false;
    } else {
      dataFile.close();
      Serial.println("Дані успішно записано у файл " + fileName);
      measAmountSD += 1;
      lastFileName += 1;
      SPI.endTransaction();
      return true;
    }
  } else {
    Serial.println("Помилка відкриття" + fileName + " для запису");
    dataFile.close();
    SPI.endTransaction();
    return false;
  }
}

String createStateString(State *_state){
  String str = "{\n";
  str += "\"light\": ";
  str += _state->light;
  str += ",\n";
  str += "\"SDcard\": ";
  str += _state->SDcard;
  str += ",\n";
  str += "\"unSyncFilesAmount\": ";
  str += _state->unSyncFilesAmount;
  str += "\n}";

  return str;
}

String createFilesList(uint16_t _from, uint16_t _to, String folder){
  unsigned long counter = 0;
  String filesList = "";
  String fileName = "";

  SPI.beginTransaction(SD_Settings);
  File root = SD.open(folder);
  yield();
  if (!root.isDirectory()){
    SPI.endTransaction();
    return "{\"data\":[\"No " + folder + " directory!\"]}";
  }
  File file = root.openNextFile();
  yield();
  if (!file){
    SPI.endTransaction();
    return "{\"data\":[\"No files found in " + folder + " directory!\"]}";
  }

  filesList += "{\n\"data\":[";
  
  while (file) {
    if (!file.isDirectory()) {
      counter++;
      yield();
      if (counter >= _from) {
        fileName = file.name();
        filesList += "\n\"";
        filesList += fileName;
        filesList += "\",";
      }
    }

    if (counter == _to) break;
    file = root.openNextFile();
    yield();
  }
  
  file.close();
  root.close();
  SPI.endTransaction();
  filesList.remove(filesList.length() - 1, 1);
  filesList += "]\n}";
  return filesList;
}
