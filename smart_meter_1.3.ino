// #include <TimeLib.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "CRC16.h"

//===Change values from here===

const char* ssid = "MyWirelessAccessPoint";
const char* password = "MyWirelessPassWord";
const char* hostName = "Smart_Meter";
const char* domoticzIP = "192.168.1.4";
const int domoticzPort = 8080;
const int domoticzGasIdx = 181;
const int domoticzEneryIdx = 183;
const bool outputOnSerial = true;
//===Change values to here===

// Vars to store meter readings
long mEVLT = 0; //Meter reading Electrics - Elektra verbruik dagtarief (Fluvius) - Totale afname van energie in kWh dagtarief
long mEVHT = 0; //Meter reading Electrics - Elektra verbruik nachttarief (Fluvius) - Totale afname van energie in kWh nachttarief
long mEOLT = 0; //Meter reading Electrics - Elektra opbrengst dagtarief (Fluvius) - Totale injectie van energie in kWh dagtarief
long mEOHT = 0; //Meter reading Electrics - Elektra opbrengst nachttarief (Fluvius) - Totale injectie van energie in kWh nachttarief
long mEAV = 0;  //Meter reading Electrics - Actueel verbruik (Fluvius) - Afgenomen ogenblikkelijk vermogen in kW 
long mEAT = 0;  //Meter reading Electrics - Actuele teruglevering (Fluvius) - Geïnjecteerd ogenblikkelijk vermogen in kW
long mGAS = 0;    //Meter reading Gas
long prevGAS = 0;


#define MAXLINELENGTH 4096 // longest normal line is 47 char (+3 for \r\n\0)
char telegram[MAXLINELENGTH];

#define SERIAL_RX     2  // pin for SoftwareSerial RX
SoftwareSerial mySerial(SERIAL_RX, -1, true, MAXLINELENGTH); // (RX, TX. inverted, buffer)

unsigned int currentCRC=0;

void SendToDomoLog(char* message)
{
  char url[512];
  sprintf(url, "http://%s:%d/json.htm?type=command&param=addlogmessage&message=%s", domoticzIP, domoticzPort, message); 
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  mySerial.begin(115200);

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(hostName);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}


bool SendToDomo(int idx, int nValue, char* sValue)
{
  HTTPClient http;
  bool retVal = false;
  char url[255];
  sprintf(url, "http://%s:%d/json.htm?type=command&param=udevice&idx=%d&nvalue=%d&svalue=%s", domoticzIP, domoticzPort, idx, nValue, sValue);
  Serial.printf("[HTTP] GET... URL: %s\n",url);
  http.begin(url); //HTTP
  int httpCode = http.GET();
  // httpCode will be negative on error
  if (httpCode > 0)
  { // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      retVal = true;
    }
  }
  else
  {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  return retVal;
}



void UpdateGas()
{
  //sends over the gas setting to domoticz
  if(prevGAS!=mGAS)
  {
    char sValue[10];
    sprintf(sValue, "%f", mGAS/1000.0);
    if(SendToDomo(domoticzGasIdx, 0, sValue))
      prevGAS=mGAS;
  }
}

void UpdateElectricity()
{
  char sValue[255];
//  sprintf(sValue, "%d;%d;%d;%d;%d;%d", mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT);  //Original value from Jan ten Hove
//  sprintf(sValue, "%f;%f;%f;%f;%d;%d", float(mEVLT)/1000.0, float(mEVHT)/1000.0, float(mEOLT)/1000.0, float(mEOHT)/1000.0, mEAV, mEAT); //First try to make kWh from Wh, but it seems slow, generating more "Invalid CRC found" error messages...is this due to required processing time?
   sprintf(sValue, "%f;%f;%f;%f;%d;%d", mEVLT/1000.0, mEVHT/1000.0, mEOLT/1000.0, mEOHT/1000.0, mEAV, mEAT); //maybe not relevant, but removing the float from first try and dividing by 1000.0 generates less "Invalid CRC found" error messages...
  SendToDomo(domoticzEneryIdx, 0, sValue);
}


bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

long getValidVal(long valNew, long valOld, long maxDiffer)
{
  //check if the incoming value is valid
      if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer)))
        return valOld;
      return valNew;
}

long getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;
  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return (1000 * atof(res));
    }
  }
  return 0;
}

bool decodeTelegram(int len) {
  //need to check for start
  int startChar = FindCharInArrayRev(telegram, '/', len);
  int endChar = FindCharInArrayRev(telegram, '!', len);
  bool validCRCFound = false;
  if(startChar>=0)
  {
    //start found. Reset CRC calculation
    currentCRC=CRC16(0x0000,(unsigned char *) telegram+startChar, len-startChar);
    if(outputOnSerial)
    {
      for(int cnt=startChar; cnt<len-startChar;cnt++)
        Serial.print(telegram[cnt]);
    }    
    //Serial.println("Start found!");
    
  }
  else if(endChar>=0)
  {
    //add to crc calc 
    currentCRC=CRC16(currentCRC,(unsigned char*)telegram+endChar, 1);
    char messageCRC[5];
    strncpy(messageCRC, telegram + endChar + 1, 4);
    messageCRC[4]=0; //thanks to HarmOtten (issue 5)
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        Serial.print(telegram[cnt]);
    }    
    validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
    if(validCRCFound)
      Serial.println("\nVALID CRC FOUND!"); 
    else
      Serial.println("\n===INVALID CRC FOUND!===");
    currentCRC = 0;
  }
  else
  {
    currentCRC=CRC16(currentCRC, (unsigned char*)telegram, len);
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        Serial.print(telegram[cnt]);
    }
  }

  long val =0;
  long val2=0;
  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik dagtarief (Fluvius) - Totale afname van energie in kWh dagtarief
  if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0) 
    mEVLT =  getValue(telegram, len);
  

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik nachttarief (Fluvius) - Totale afname van energie in kWh nachttarief
  if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0) 
    mEVHT = getValue(telegram, len);
    

  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst dagtarief (Fluvius) - Totale injectie van energie in kWh dagtarief
  if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0) 
    mEOLT = getValue(telegram, len);
   

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst nachttarief (Fluvius) - Totale injectie van energie in kWh nachttarief
  if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0) 
    mEOHT = getValue(telegram, len);
    

  // 1-0:1.7.0(00.424*kW) Actueel verbruik (Fluvius) - Afgenomen ogenblikkelijk vermogen in kW 
  // 1-0:2.7.0(00.000*kW) Actuele teruglevering (Fluvius) - Geïnjecteerd ogenblikkelijk vermogen in kW
  // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0) 
    mEAV = getValue(telegram, len);
    
  if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0)
    mEAT = getValue(telegram, len);
   

  // 0-1:24.2.1(150531200000S)(00811.923*m3)
  // 0-1:24.2.3 = Gas (Fluvius) on Siconia S211 meter
  if (strncmp(telegram, "0-1:24.2.3", strlen("0-1:24.2.3")) == 0) 
    mGAS = getValue(telegram, len);

  return validCRCFound;
}

void readTelegram() {
  if (mySerial.available()) {
    memset(telegram, 0, sizeof(telegram));
    while (mySerial.available()) {
      int len = mySerial.readBytesUntil('\n', telegram, MAXLINELENGTH);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();
      if(decodeTelegram(len+1))
      {
         UpdateElectricity();
         UpdateGas();
      }
    } 
  }
}



void loop() {
  readTelegram();
  ArduinoOTA.handle();
}
