#include <Wire.h>
#include <SeeedGrayOLED.h>
#include <avr/pgmspace.h>
#include <SoftwareSerial.h>
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <ArduinoJson.h>
//#include <Time.h>  
#include "printf.h"

#define LED 4
#define RF_CS 9
#define RF_CSN 10

RF24 radio(RF_CS, RF_CSN);
RF24Network network(radio);          // Network uses that radio
const uint16_t this_node = 00;        // Address of our node in Octal format
const uint16_t other_node = 01;       // Address of the other node in Octal format

char message[128] = { 0 };
const char* version = "0.40";
uint8_t currentLine = 4;  // for OLED roll
#define OLED_ROWS 12
#define OLED_LINE_LENGTH 12
const char* oledBlankLine = "            ";

char inputString[64] = { 0 };         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
boolean readMore = true;        // true if serial read incomplete
int32_t time_offset;   // offset in seconds when date/time received
uint32_t seconds_since_midnight;
uint16_t day_of_year;

#define MAX_SENSORS 4
uint32_t lastRead = 0;  // milliseconds at last sensor data received
uint32_t lastUpdate = 0;
struct sensorsStruct {
  uint8_t sid;
  float f;
  float h;
  bool pir;
  long bs;  // percent full
  char version[8];
  uint32_t lastRead;
  uint16_t interval;
};

sensorsStruct sensors[MAX_SENSORS];
#define rxPin 6
#define txPin 7
SoftwareSerial mySerial(rxPin, txPin); // RX, TX

void blink(int n) {
  for (int i = 1; i <= n; i++) {
    digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(500);               // wait for a bit
    digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
  }
}

void setup()
{
  // define pin modes for tx, rx:
  pinMode(rxPin, INPUT);
  pinMode(txPin, OUTPUT);
  mySerial.begin(19200);  //Start the Serial at 19200 baud: software serial can't do more :(
  printf_begin();
  mySerial.flush();
  mySerial.print("RF24 v"); mySerial.print(version); mySerial.println(" receiver starting...");
  pinMode(LED, OUTPUT);
  blink(1);
  //randomSeed (analogRead(A0));  //initialize the random number generator with
  //a random read from an unused and floating analog port
  radio.begin();
  //radio.setDataRate(RF24_250KBPS);
  //radio.setPALevel(RF24_PA_MAX);
  //radio.printDetails();
  network.begin(/*channel*/ 90, /*node address*/ this_node);
  // send startup message to Pi to get configuration
  mySerial.println("[startup]");
  // setup OLED
  Wire.begin();
  SeeedGrayOled.init();
  SeeedGrayOled.clearDisplay();
  SeeedGrayOled.setNormalDisplay();
  SeeedGrayOled.setVerticalMode();
  SeeedGrayOled.setGrayLevel(6);
  oledPutLine(0, 0, "Pi Rcvr ", true);
  oledPutLine(0, 8, (char*)version, false);
  oledPutLine(2, 0, "Startup!", true);
  oledPutLine(3, 0, "------------", false);
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

void oledPutLine(int line, int column, char* msg, bool clear) {
  if (clear) oledClearLine(line);
  SeeedGrayOled.setTextXY(line, column);
  SeeedGrayOled.putString(msg);
}

void processConfig(char* msg) {
  // Pi Monitor sends config string to us or a msg
  // msg may be json config or just a string
  mySerial.print("echo len="); mySerial.print(strlen(msg));
  mySerial.print(":"); mySerial.println(msg);
  mySerial.flush();
  if (msg[0] == '{') {
    StaticJsonBuffer<32> jsonBuffer;
    char json[32];
    memcpy(json, msg, strlen(msg) + 1); // copy since json parser overwrites buffer
    // parse json from server
    JsonObject& root = jsonBuffer.parseObject(msg);

    if (!root.success()) {
      mySerial.println("Receiver parseObject() config failed: "); mySerial.print("--"); mySerial.println(json);
      mySerial.flush();
      return;
    } else {
      uint8_t sid = root["id"];
      sid = sid - 1;
      if (root.containsKey("id")) {
        sensors[sid].interval = root["id"];
        if (root.containsKey("t")) {
          seconds_since_midnight = root["t"];
          mySerial.print("sec since midnight: "); mySerial.println(seconds_since_midnight);
          time_offset = millis() / 1000;
        }
        if (root.containsKey("d")) {
          day_of_year = root["d"];
          mySerial.print("day of year: "); mySerial.println(day_of_year);
        }
      } else {
        mySerial.println("No id found in json");
        mySerial.flush();
        return;
      }
      // pass config json on to the targeted sensor
      RF24NetworkHeader header(/*to node*/ sid + 1, /*type*/ 'c');
      bool sendOK = network.write(header, json, strlen(json) + 1);
      if (!sendOK) {
        mySerial.print("Write to "); mySerial.print(sid + 1); mySerial.print(" failed: "); mySerial.println(json);
      } else {
        mySerial.print("Sent to "); mySerial.print(sid + 1); mySerial.print(":"); mySerial.println(json);
      }
      mySerial.flush();
    }
  } else {
    mySerial.print("Status: "); mySerial.println(msg);
    mySerial.flush();
    oledPutLine(2, 0, msg, true);
  }
}

int freeRam ()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

void oledUpdate(int sid) {
  // TODO: add vertical scrolling to handle more than 4 sensors
  // sprintf does not do float formatting on Arduino :(
  uint32_t now = millis();
  char l1[13] = {0}; char l2[20] = {0};
  int cline;
  const char *l1_mask = "%u %s  %s";
  const char *l2_mask = "%02u%02u %2u%% m:%u";
  uint32_t ssm = ((now / 1000L) - time_offset) + seconds_since_midnight; // seconds since midnight
  uint16_t days = ssm / 86400L;
  ssm = ssm % 86400L;
  uint16_t hh = ssm / 3600L;
  uint16_t mm = (ssm - (hh * 3600L)) / 60;
  uint16_t ss = ssm % 60;
  sprintf(l1, "%03u.%02u:%02u:%02u", day_of_year + days, hh, mm, ss);
  //mySerial.print(hh);mySerial.print(mm);mySerial.println(ss);
  oledPutLine(1, 0, l1, false);
  if (sid < 0) {
    if ((now - lastUpdate) < 60000L) return;
  }
  lastUpdate = now;
  //mySerial.print("free1=");mySerial.println(freeRam());
  for (int i = 0; i < MAX_SENSORS; i++) {
    if (sensors[i].lastRead == 0) continue;
    int gray = 4;
    if (sid == i) gray = 9;
    SeeedGrayOled.setGrayLevel(gray);
    cline = 2 * i + 4;
    if (sensors[i].lastRead == lastRead) {
      oledClearLine(cline);
      char fString[8], hString[8];
      dtostrf(sensors[i].f, 3, 1, fString);
      dtostrf(sensors[i].h, 3, 1, hString);
      memset(l1, 0, sizeof(l1));  // clear the buffer
      snprintf(l1, sizeof(l1), l1_mask, sensors[i].sid, fString, hString);
      oledPutLine(cline, 0, l1, false);
      //mySerial.println(l1);
    }
    oledClearLine(cline + 1);
    memset(l2, 0, sizeof(l2));  // clear the buffer
    uint8_t bs = sensors[i].bs > 0 ? sensors[i].bs : 0;
    uint8_t pir = sensors[i].pir ? 1 : 0;
    ssm = (lastRead - sensors[i].lastRead) / 1000L; // seconds since last read
    ssm = ssm % 86400L;
    hh = ssm / 3600L;
    mm = (ssm - (hh * 3600L)) / 60;
    // WARNING: snprintf seems to overlay storage past end
    snprintf(l2, sizeof(l2), l2_mask, hh, mm, bs, pir);
    //mySerial.println(l2);
    //mySerial.flush();
    oledPutLine(cline + 1, 0, l2, false);
  }
  //mySerial.print("free3=");mySerial.println(freeRam());
}

void loop() {
  oledUpdate(-1);

  if (readMore && mySerial.available()) {
    while (mySerial.available()) {
      // get the new byte:
      char inChar = (char)mySerial.read();
      // add it to the inputString:
      uint16_t inLen = strlen(inputString);
      inputString[inLen] = inChar;
      inputString[inLen + 1] = '\0';
      // if the incoming character is a newline, set a flag
      // so the main loop can do something about it:
      if ((inChar == '\n') || (strlen(inputString) >= sizeof(inputString))) {
        stringComplete = true;
        readMore = false;
        break;
      }
    }
  }

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
  lastRead = millis();
  // can only receive 24 bytes of data!!!
  while ( network.available() ) {     // Is there anything ready for us?
    StaticJsonBuffer<64> jsonBuffer;
    RF24NetworkHeader header;        // If so, grab it and print it out

    network.peek(header);
    uint8_t payload[25];  // we'll receive a packet
    memset(payload, 0, sizeof(payload));  // clear the buffer
    int count = network.read(header, &payload, sizeof(payload));
    payload[count] = 0;  // terminate buffer
    char oline[2 * OLED_LINE_LENGTH + 1];
    sprintf(oline, "%d->%d %c %d", header.from_node, header.to_node, header.type ? header.type : '-', strlen((const char*)&payload));
    oledPutLine(3, 0, oline, true);
    if (header.from_node == 0) {
      delay(5000);
      continue;
    }
    mySerial.print("Received "); mySerial.print(count);
    mySerial.print(" from "); mySerial.print(header.from_node);
    mySerial.print("->"); mySerial.print(header.to_node);
    mySerial.print(" id "); mySerial.print(header.id);
    mySerial.print(" type "); mySerial.print((char)header.type);
    mySerial.print(" len="); mySerial.println(strlen((const char*)&payload));
    if (strlen((const char*)&payload) == 0) continue;
    // insert sensor id into json message
    if ( payload[0] == '{') {
      char payloadWithId[32];
      sprintf(payloadWithId, "{\"id\":%d,%s", header.from_node, &payload[1]);
      mySerial.println((const char*)&payloadWithId);  // sends json data to the pi for handling
      // parse json from sensor
      JsonObject& root = jsonBuffer.parseObject((char*)payload);
      if (!root.success()) {
        mySerial.print("parseObject() sensor failed:"); mySerial.println((const char*)payload);
        continue;
      }
      int sid = header.from_node;
      sid = sid - 1;
      if (header.type == 'c') {
        if (root.containsKey("v")) {
          const char* v = root["v"];
          strncpy ( sensors[sid].version, v, sizeof(sensors[sid].version) );
        }
      }
      if (header.type == 'S') {
        sensors[sid].lastRead = lastRead;
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
        //mySerial.print("rcvd id=");mySerial.print(sid);mySerial.print(" t=");mySerial.print(f);
        //mySerial.print(" h=");mySerial.println(h);
        oledUpdate(sid);
      }
    } else {
      // pass thru info message
      mySerial.println((const char*)payload);
    }
    mySerial.flush(); // wait for write to complete so we don't overwrite buffer
  }
}

