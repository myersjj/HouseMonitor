#include <Wire.h>
#include <SeeedGrayOLED.h>
#include <avr/pgmspace.h>

#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "printf.h"

#define LED 4
#define RF_CS 9
#define RF_CSN 10

#define I2C_ADDRESS 0x04

RF24 radio(RF_CS, RF_CSN);
RF24Network network(radio);          // Network uses that radio
const uint16_t this_node = 00;        // Address of our node in Octal format
const uint16_t other_node = 01;       // Address of the other node in Octal format

char message[128] = { 0 };
const char* version = "0.36";
uint8_t currentLine = 4;  // for OLED roll
#define MAX_LINES 12
const char* oledBlankLine = "            ";

char inputString[64] = { 0 };         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
boolean readMore = true;        // true if serial read incomplete
int32_t time_offset;   // offset in seconds when date/time received
uint32_t seconds_since_midnight;
uint16_t day_of_year;

#define MAX_SENSORS 5
struct sensorsStruct {
  uint8_t sid;
  float f;
  float h;
  bool pir;
  long bs;  // percent full
  uint32_t lastRead;
  uint16_t interval;
};

sensorsStruct sensors[MAX_SENSORS];

void blink(int n) {
  for (int i = 1; i <= n; i++) {
    digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(500);               // wait for a bit
    digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
  }
}

void setup()
{
  Serial.begin(57600);  //Start the Serial at 57600 baud
  printf_begin();
  Serial.flush();
  Serial.print("RF24 v"); Serial.print(version); Serial.println(" receiver starting...");
  //fflush(stdout);
  pinMode(LED, OUTPUT);
  blink(1);
  //randomSeed (analogRead(A0));  //initialize the random number generator with
  //a random read from an unused and floating analog port
  radio.begin();
  //radio.printDetails();
  network.begin(/*channel*/ 90, /*node address*/ this_node);
  // send startup message to Pi to get configuration
  Serial.println("[startup]");
  // test OLED
  Wire.begin();
  SeeedGrayOled.init();
  SeeedGrayOled.clearDisplay();
  SeeedGrayOled.setNormalDisplay();
  SeeedGrayOled.setVerticalMode();
  SeeedGrayOled.setTextXY(0, 0);
  SeeedGrayOled.setGrayLevel(6);
  SeeedGrayOled.putString("Pi Rcvr ");
  SeeedGrayOled.setTextXY(0, 8);
  SeeedGrayOled.putString(version);
  SeeedGrayOled.setTextXY(2, 0);
  SeeedGrayOled.putString("Startup!");
  SeeedGrayOled.setTextXY(3, 0);
  SeeedGrayOled.putString("------------");
  // init sensor array
  for (int i = 0; i < MAX_SENSORS; i++) {
    sensors[i].sid = i + 1;
    sensors[i].lastRead = 0;
    sensors[i].f = -1.0;
    sensors[i].h = -1.0;
    sensors[i].pir = false;
    sensors[i].bs = 0L;
    sensors[i].interval = 300;  // default is 300 seconds
  }
}

void oledClearLine(int line) {
  // clear out line
  SeeedGrayOled.setTextXY(line, 0);
  SeeedGrayOled.putString(oledBlankLine);
}

void processConfig(char* msg) {
  // Pi Monitor sends config string to us or a msg
  // msg may be json config or just a string
  Serial.print("echo len="); Serial.print(strlen(msg));
  Serial.print(":"); Serial.println(msg);
  Serial.flush();
  if (msg[0] == '{') {
    StaticJsonBuffer<32> jsonBuffer;
    char json[32];
    memcpy(json, msg, strlen(msg)+1);  // copy since json parser overwrites buffer 
    // parse json from sensor
    JsonObject& root = jsonBuffer.parseObject(msg);

    if (!root.success()) {
      Serial.println("Receiver parseObject() config failed: ");Serial.print("--");Serial.println(json);
      Serial.flush();
      return;
    } else {
      uint8_t sid = root["id"];
      sid = sid - 1;
      if (root.containsKey("id")) {
        sensors[sid].interval = root["id"];
        if (root.containsKey("t")) {
          seconds_since_midnight = root["t"];
          Serial.print("sec since midnight: ");Serial.println(seconds_since_midnight);
          time_offset = millis()/1000;
        }
        if (root.containsKey("d")) {
          day_of_year = root["d"];
          Serial.print("day of year: ");Serial.println(day_of_year);
        }
      } else {
        Serial.println("No id found in json");
        Serial.flush();
        return;
      }
      // pass config json on to the targeted sensor
      RF24NetworkHeader header(/*to node*/ sid+1, /*type*/ 'c');
      bool sendOK = network.write(header, json, strlen(json)+1);
      if (!sendOK) {
        Serial.print("Write to "); Serial.print(sid+1); Serial.print(" failed: "); Serial.println(json);
      } else {
        Serial.print("Sent to ");Serial.print(sid+1); Serial.print(":");Serial.println(json);
      }
      Serial.flush();
    }
  } else {
    Serial.print("Status: "); Serial.println(msg);
    Serial.flush();
    oledClearLine(2);
    SeeedGrayOled.setTextXY(2, 0);
    SeeedGrayOled.putString(msg);
  }
}

int freeRam ()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

void oledUpdate(int sid) {  // oled uses 1K of SRAM!!!
  // TODO: add vertical scrolling to handle more than 4 sensors
  // sprintf does not do float formatting on Arduino :(
  char l1[13] = {0}; char l2[13] = {0};
  int cline;
  const char *l1_mask = "%u %s  %s";
  const char *l2_mask = "   %3d%% m:%u";
  SeeedGrayOled.setTextXY(1, 0);
  uint32_t ssm = ((millis()/1000) - time_offset) + seconds_since_midnight;  // seconds since midnight
  uint16_t days = ssm/86400;
  ssm = ssm % 86400;
  uint16_t hh = ssm/3600L;
  uint16_t mm = (ssm - (hh*3600L))/60;
  uint16_t ss = ssm % 60;
  sprintf(l1, "%03u.%02u:%02u:%02u", day_of_year+days, hh, mm, ss);
  //Serial.print(hh);Serial.print(mm);Serial.println(ss);
  SeeedGrayOled.putString(l1);
  if (sid < 0) return;
  //Serial.print("free1=");Serial.println(freeRam());
  for (int i = 0; i < MAX_SENSORS; i++) {
    int gray = 4;
    if (sid == i) gray = 9;
    SeeedGrayOled.setGrayLevel(gray);
    cline = 2 * i + 4;
    oledClearLine(cline);
    oledClearLine(cline + 1);
    char fString[8], hString[8];
    dtostrf(sensors[i].f, 3, 1, fString);
    dtostrf(sensors[i].h, 3, 1, hString);
    memset(l1, 0, sizeof(l1));  // clear the buffer
    snprintf(l1, sizeof(l1), l1_mask, sensors[i].sid, fString, hString);
    SeeedGrayOled.setTextXY(cline, 0);
    SeeedGrayOled.putString(l1);
    //Serial.println(l1);
    memset(l2, 0, sizeof(l2));  // clear the buffer
    snprintf(l2, sizeof(l2), l2_mask, sensors[i].bs, sensors[i].pir ? 1 : 0);
    //Serial.println(l2);
    //Serial.flush();
    SeeedGrayOled.setTextXY(cline + 1, 0);
    SeeedGrayOled.putString(l2);
  }
  //Serial.print("free3=");Serial.println(freeRam());
}

void loop() {
  oledUpdate(-1);

#if 1
  if (readMore && Serial.available()) {
    while (Serial.available()) {
      // get the new byte:
      char inChar = (char)Serial.read();
      // add it to the inputString:
      uint16_t inLen = strlen(inputString);
      inputString[inLen] = inChar;
      inputString[inLen+1] = '\0';
      // if the incoming character is a newline, set a flag
      // so the main loop can do something about it:
      if ((inChar == '\n') || (strlen(inputString) >= sizeof(inputString))) {
        stringComplete = true;
        readMore = false;
        break;
      }
    }
  }
#endif
  if (stringComplete) {
    // process message from server
    // get node id from message and send it to node
    processConfig(inputString);
    // clear the string:
    inputString[0] = '\0';
    readMore = true;
    stringComplete = false;
  }
  
  network.update();                  // Check the network regularly
  // can only receive 24 bytes of data!!!
  while ( network.available() ) {     // Is there anything ready for us?
    StaticJsonBuffer<64> jsonBuffer;
    RF24NetworkHeader header;        // If so, grab it and print it out
    
    network.peek(header);
    uint8_t payload[25];  // we'll receive a packet
    memset(payload, 0, sizeof(payload));  // clear the buffer
    int count = network.read(header, &payload, sizeof(payload));
    payload[count] = 0;  // terminate buffer
    Serial.print("Received "); Serial.print(count);
    Serial.print(" from "); Serial.print(header.from_node);
    Serial.print("->"); Serial.print(header.to_node);
    Serial.print(" id "); Serial.print(header.id);
    Serial.print(" type "); Serial.print((char)header.type);
    Serial.print(" len="); Serial.println(strlen((const char*)&payload));
    if (header.from_node == 0) continue;
    if (strlen((const char*)&payload) == 0) continue;
    // insert sensor id into json message
    if ( payload[0] == '{') {
      char payloadWithId[32];
      sprintf(payloadWithId, "{\"id\":%d,%s", header.from_node, &payload[1]);
      Serial.println((const char*)&payloadWithId);  // sends json data to the pi for handling
      } else {
        Serial.println((const char*)&payload);  // sends json data to the pi for handling
      }
    Serial.flush(); // wait for write to complete so we don't overwrite buffer

    if (header.type == 'S') {
    // parse json from sensor
    JsonObject& root = jsonBuffer.parseObject((char*)payload);

    if (!root.success()) {
      Serial.print("parseObject() sensor failed:");Serial.println((const char*)payload);
    } else {
      //int sid = root["id"];
      int sid = header.from_node;
      sid = sid - 1;
      sensors[sid].lastRead = millis();
      bool pir = false;
      if (root.containsKey("rh")) {
        sensors[sid].h = root["rh"];
      }
      if (root.containsKey("tf")) {
        sensors[sid].f = root["tf"];
      }
      if (root.containsKey("bs")) {
        sensors[sid].bs = root["bs"];
      }
      if (root.containsKey("pir")) {
        pir = root["pir"];
        sensors[sid].pir = pir;
      }
      //Serial.print("rcvd id=");Serial.print(sid);Serial.print(" t=");Serial.print(f);
      //Serial.print(" h=");Serial.println(h);
      oledUpdate(sid);
    }
    } else {
      // pass thru info message
      Serial.println((const char*)payload);
    }
  }
}

