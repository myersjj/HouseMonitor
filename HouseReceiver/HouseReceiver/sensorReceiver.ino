#include <Wire.h>
#include <SeeedGrayOLED.h>
#include <avr/pgmspace.h>

#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <ArduinoJson.h>

#define LED 4
#define RF_CS 9
#define RF_CSN 10

#define I2C_ADDRESS 0x04

RF24 radio(RF_CS, RF_CSN);
RF24Network network(radio);          // Network uses that radio
const uint16_t this_node = 00;        // Address of our node in Octal format
const uint16_t other_node = 01;       // Address of the other node in Octal format

char message[128] = { 0 };
const char* version = "0.31";
uint8_t currentLine = 4;  // for OLED roll
#define MAX_LINES 12
const char* oledBlankLine = "            ";

char inputString[64] = { 0 };         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
boolean readMore = true;        // true if serial read incomplete

#define MAX_SENSORS 4
struct sensorsStruct {
  uint8_t sid;
  float f;
  float h;
  bool pir;
  long lastRead;
  int interval;
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
  Serial.flush();
  Serial.print("RF24 v"); Serial.print(version); Serial.println(" receiver starting...");
  //fflush(stdout);
  pinMode(LED, OUTPUT);
  blink(1);
  randomSeed (analogRead(A0));  //initialize the random number generator with
  //a random read from an unused and floating analog port
  radio.begin();
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
  SeeedGrayOled.putString("Pi Receiver");
  SeeedGrayOled.setTextXY(1, 0);
  SeeedGrayOled.putString("Version ");
  SeeedGrayOled.setTextXY(1, 8);
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
    sensors[i].interval = 30;  // default is 30 seconds
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
    memcpy(json, msg, strlen(msg));  // copy since json parser overwrites buffer 
    // parse json from sensor
    JsonObject& root = jsonBuffer.parseObject((char*)msg);

    if (!root.success()) {
      Serial.println("parseObject() config failed");
    } else {
      int sid = root["id"];
      sid = sid - 1;
      if (root.containsKey("i")) {
        sensors[sid].interval = root["i"];
      }
      // pass config json on to the targeted sensor
      RF24NetworkHeader header(/*to node*/ sid+1, /*type*/ 'c');
      bool sendOK = network.write(header, json, strlen(json)); // add 1 to include terminator
      if (!sendOK) {
        Serial.print("Write buffer to "); Serial.print(sid+1); Serial.print(" failed: "); Serial.println(json);
      } else {
        Serial.print("Sent to ");Serial.print(sid+1); Serial.print(":");Serial.println(json);
      }
    }
  } else {
    Serial.print("Status: "); Serial.println(msg);
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
  const char *f_mask = "%u f:%s";
  const char *h_mask = "  h:%s m:%u";
  //Serial.print("free1=");Serial.println(freeRam());
  for (int i = 0; i < MAX_SENSORS; i++) {
    int gray = 4;
    if (sid == i) gray = 9;
    SeeedGrayOled.setGrayLevel(gray);
    cline = 2 * i + 4;
    oledClearLine(cline);
    oledClearLine(cline + 1);
    char fString[8];
    dtostrf(sensors[i].f, 3, 1, fString);
    memset(l1, 0, sizeof(l1));  // clear the buffer
    sprintf(l1, f_mask, sensors[i].sid, fString);
    SeeedGrayOled.setTextXY(cline, 0);
    SeeedGrayOled.putString(l1);
    //Serial.println(l1);
    dtostrf(sensors[i].h, 3, 1, fString);
    memset(l2, 0, sizeof(l2));  // clear the buffer
    sprintf(l2, h_mask, fString, sensors[i].pir);
    SeeedGrayOled.setTextXY(cline + 1, 0);
    SeeedGrayOled.putString(l2);
  }
  //Serial.print("free3=");Serial.println(freeRam());
}

void loop() {
  StaticJsonBuffer<64> jsonBuffer;

  network.update();                  // Check the network regularly
#if 1
  if (readMore && Serial.available()) {
    while (Serial.available()) {
      // get the new byte:
      char inChar = (char)Serial.read();
      // add it to the inputString:
      strncat(inputString, &inChar, 1);
      strncat(inputString, '\0', 1);  // terminate it
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
  // can only receive 24 bytes of data!!!
  while ( network.available() ) {     // Is there anything ready for us?

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
    Serial.print(" type "); Serial.print(header.type);
    Serial.print(" len="); Serial.println(strlen((const char*)&payload));
    Serial.println((const char*)&payload);  // sends json data to the pi for handling
    Serial.flush(); // wait for write to complete so we don't overwrite buffer
#if 0
    oledClearLine(currentLine + 1);
    for (int k = 0; k < count; k++) {
      SeeedGrayOled.setTextXY(currentLine + (k / 12), k % 12);
      SeeedGrayOled.putChar(payload[k]);
    }
    currentLine += 2;
    if (currentLine >= MAX_LINES) currentLine = 4;
#endif
    if (header.type == 'S') {
    // parse json from sensor
    JsonObject& root = jsonBuffer.parseObject((char*)payload);

    if (!root.success()) {
      Serial.print("parseObject() sensor failed:");Serial.println((const char*)payload);
    } else {
      int sid = root["id"];
      sid = sid - 1;
      sensors[sid].lastRead = millis();
      bool pir = false;
      if (root.containsKey("rh")) {
        sensors[sid].h = root["rh"];
      }
      if (root.containsKey("tf")) {
        sensors[sid].f = root["tf"];
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
#if 0
/*
  SerialEvent occurs whenever new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 Arduino only has 64 byte receive buffer, so may need multiple loops to read whole message.
 */
void serialEventxx() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    // add it to the inputString:
    strcatc(inputString, inChar);
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if ((inChar == '\n') || (strlen(inputString) >= sizeof(inputString))) {
      stringComplete = true;
      readMore = false;
      break;
    }
  }
  if (!stringComplete) readMore = true;
}
#endif
