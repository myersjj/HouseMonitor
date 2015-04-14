// House sensor sketch - James J Myers
#include <RF24.h>
#include <RF24Network.h>
#include <SPI.h>
#include <JeeLib.h>
#include <DHT22.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <printf.h>

#define SENSOR_NANO 1  // Arduino Nano
#define SENSOR_UNO 2   // Arduino Uno
#define SENSOR_MC8 3   // 8 MHz atmega328
#define SENSOR_MINI 4  // 8 MHz Mini Pro

#define NODE_ID 02     // Change this_node for each board you deploy.

#if NODE_ID == 01
#define SENSOR_MODEL SENSOR_MC8
#define DHT22_PIN 2
#define PIR_PIN 0  // use 0 if no PIR on this sensor
#elif NODE_ID == 02
#define SENSOR_MODEL SENSOR_MC8
#define DHT22_PIN 2
#define PIR_PIN 0  // use 0 if no PIR on this sensor
#elif NODE_ID == 03
#define SENSOR_MODEL SENSOR_MINI
#define DHT22_PIN 2
#define PIR_PIN 0  // use 0 if no PIR on this sensor
#elif NODE_ID == 04
#define SENSOR_MODEL SENSOR_MC8
#define DHT22_PIN 2
#define PIR_PIN 0  // use 0 if no PIR on this sensor
#else
#  define PIR_PIN 0  // use 0 if no PIR on this sensor
#endif

const uint16_t this_node = NODE_ID;        // Address of our node in Octal format
const uint16_t base_node = 00;       // Address of the other node in Octal format

#define DHT_PIN DHT22_PIN
// Pin 13 has an LED connected on most Arduino boards.
#define LED 13
#define RF_CS 9
#define RF_CSN 10

#if SENSOR_MC8 == SENSOR_MODEL
#define DHT_DELAY 30  // for 8mhz boards
#else
#define DHT_DELAY 6  // default
#endif
// Connect pin 1 (on the left) of the sensor to +5V
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor
#define BATTERY_PIN 0  // use 0 if no battery
#define UNIT 0      // 0 for Fahrenheit and 1 for Celsius

int eeAddress = 0; //EEPROM address to start reading from
struct MyObject {
  bool initialized;
  uint32_t restarts;
  uint8_t sensorId;
  uint16_t interval;
  char name[16];
};

RF24 radio(RF_CS, RF_CSN);
RF24Network network(radio);          // Network uses that radio

//DHT dht(DHT_PIN, DHTTYPE, DHT_DELAY);
DHT22 myDHT22(DHT22_PIN);

bool motionDetected = false;
uint16_t config_interval = 300;
MyObject sensorConfig; //Variable to store custom object read from EEPROM.

ISR(WDT_vect) {
  Sleepy::watchdogEvent();
} // Setup the watchdog

void blink(int n) {
  for (int i = 1; i <= n; i++) {
    digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(1000);               // wait for a bit
    digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
  }
}

boolean sendMessage(char *buffer, char msgtype) {
  // transmit the data
  //Serial.print("\nTransmitting..."); Serial.println(remaining);
  Serial.flush();
  // RF24 only writes 32 bytes max in each payload, 8 byte header and 24 data, so split it up if needed
  int bufsize = strlen(buffer);
  buffer[bufsize] = '\0';  // ensure buffer is terminated properly
  RF24NetworkHeader header(/*to node*/ base_node, /*type*/ msgtype);
  Serial.print("Sending packet to "); Serial.print(base_node); Serial.print(" at ");Serial.print(millis()); 
  Serial.print(" ");Serial.print(msgtype); Serial.print(":"); Serial.println(bufsize);
  bool sendOK = network.write(header, buffer, bufsize + 1); // add 1 to include terminator
  if (!sendOK) {
    Serial.println("Write buffer failed");
    // signal error
    blink(1);
  } else {
    Serial.print("sent="); Serial.print(buffer); Serial.println("...");
  }
  Serial.flush();
  return sendOK;
}

void setup() {
  Serial.begin(57600);
  printf_begin();
  Serial.println("Starting v0.34...");

  EEPROM.get( eeAddress, sensorConfig );
  if (!sensorConfig.initialized) {
    sensorConfig.initialized = true;
    sensorConfig.restarts = 0L;
    sensorConfig.sensorId = this_node;
    sensorConfig.interval = config_interval;
  }
  sensorConfig.restarts += 1;
  config_interval = sensorConfig.interval;
  config_interval = (config_interval<0) ? 300 : config_interval;
  config_interval = (config_interval>3600) ? 300 : config_interval;
  EEPROM.put( eeAddress, sensorConfig );
  Serial.print("restarts="); Serial.print(sensorConfig.restarts);
  Serial.print(" DHT "); Serial.print(DHT_PIN);
  Serial.print(" PIR "); Serial.print(PIR_PIN);
  Serial.print(" bat "); Serial.print(BATTERY_PIN);
  Serial.print(" int "); Serial.println(config_interval);
  pinMode(LED, OUTPUT);
  if (PIR_PIN > 0) {
    //pinMode(PIR_PIN, INPUT_PULLUP);
    pinMode(PIR_PIN, INPUT);
  }
  
  //randomSeed (analogRead(A0));  //initialize the random number generator with
  //a random read from an unused and floating analog port

  //Initialize the Sensor
  radio.begin();
  network.begin(/*channel*/ 90, /*node address*/ this_node); // deprecated
  //network.begin(/*node address*/ this_node);
  Serial.print("timeout=");Serial.println(network.txTimeout);
  network.txTimeout = 15;
  radio.printDetails();
  bool sendOK = false;
  Serial.flush();
  while (!sendOK) {
    sendOK = sendMessage("[config]", 'c');
    if (sendOK) break;
    Serial.println("request config failed...");
    blink(1);
    break;
  }
  delay(2000);  // delay to allow DHT22 to stabilize
  char buffer[24];
  sprintf(buffer, "ID:%d DHT:%d PIR:%d", this_node, DHT_PIN, PIR_PIN);
  sendMessage(buffer, 'i');
  Serial.flush();
  blink(this_node);
}

int ftoa(char *a, float f) //translates floating point readings into strings to send over the air
{
  int left = int(f);
  float decimal = f - left;
  int right = decimal * 100; //2 decimal points
  if (right > 10) {  //if the decimal has two places already. Otherwise
    sprintf(a, "%d.%d", left, right);
  } else {
    sprintf(a, "%d.0%d", left, right); //pad with a leading 0
  }
  return 1;
}

void loop() {
  network.update();                          // Check the network regularly

  // can only receive 24 bytes of data!!!
  while ( network.available() ) {     // Is there anything ready for us?
    RF24NetworkHeader header;        // If so, grab it and process it
    network.peek(header);
    Serial.print("header="); Serial.println((char)header.type);
    uint8_t payload[25];  // we'll receive a packet
    memset(payload, 0, sizeof(payload));  // clear the buffer
    int count = network.read(header, &payload, sizeof(payload));
    payload[count] = 0;  // terminate buffer
    unsigned int interval = 0;
    switch (header.type) {
      case 'c': {
          // parse config json from manager
          StaticJsonBuffer<64> jsonBuffer;
          JsonObject& root = jsonBuffer.parseObject((char*)payload);

          if (!root.success()) {
            Serial.println("parseObject() sensor config failed");
            Serial.println((char*)payload);
          } else {
            uint8_t sid = root["id"];
            sid = sid - 1;
            if (root.containsKey("i")) {
              interval = root["i"];
            }
            config_interval = interval > 20 ? interval : 30;
            config_interval = (config_interval <= 3600) ? config_interval : 300;
            sensorConfig.interval = config_interval;
            EEPROM.put( eeAddress, sensorConfig );
            Serial.print("interval="); Serial.println(config_interval);
          }
          break;
        }
      default:
        Serial.print('Invalid msg type:'); Serial.println(header.type);
        break;
    }
  }

  // get sensor inputs, then sleep
  //Serial.println("reading pir");
  bool pir = false;
  if (PIR_PIN) {
    uint8_t pirv = digitalRead(PIR_PIN);
    //Serial.print("pirv=");Serial.println(pirv);
    pir = (pirv == 1) ? true : false;
  }

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  //Serial.println("Reading DHT...");
  float h = -1;
  float t = -1;
  float f = -1;
#ifdef DHT22_PIN
  DHT22_ERROR_t errorCode;
  errorCode = myDHT22.readData();
  switch (errorCode)
  {
    case DHT_ERROR_NONE: {
        t = myDHT22.getTemperatureC();
        f = t * 1.8 + 32.0;
        h = myDHT22.getHumidity();
        // Alternately, with integer formatting which is clumsier but more compact to store and
        // can be compared reliably for equality:
        //
        //char buf[128];
        //sprintf(buf, "Integer-only reading: Temperature %hi.%01hi C, Humidity %i.%01i %% RH",
        //        myDHT22.getTemperatureCInt() / 10, abs(myDHT22.getTemperatureCInt() % 10),
        //        myDHT22.getHumidityInt() / 10, myDHT22.getHumidityInt() % 10);
        //Serial.println(buf);
        break;
      }
    case DHT_ERROR_CHECKSUM:
      Serial.print("check sum error ");
      Serial.print(myDHT22.getTemperatureC());
      Serial.print("C ");
      Serial.print(myDHT22.getHumidity());
      Serial.println("%");
      break;
    case DHT_BUS_HUNG:
      Serial.println("BUS Hung ");
      break;
    case DHT_ERROR_NOT_PRESENT:
      Serial.println("Not Present ");
      break;
    case DHT_ERROR_ACK_TOO_LONG:
      Serial.println("ACK time out ");
      break;
    case DHT_ERROR_SYNC_TIMEOUT:
      Serial.println("Sync Timeout ");
      break;
    case DHT_ERROR_DATA_TIMEOUT:
      Serial.println("Data Timeout ");
      break;
    case DHT_ERROR_TOOQUICK:
      Serial.println("Polled too quick ");
      break;
  }
#endif
#ifndef DHT22_PIN
  h = dht.readHumidity();
  t = dht.readTemperature();
  f = dht.readTemperature(true);
#endif
#if 1
  Serial.print("Humidity: ");
  Serial.print(h);
  Serial.print(" %\t");
  Serial.print("Temperature: ");
  Serial.print(t);
  Serial.print(" *C ");
  Serial.print(f);
  Serial.println(" *F\t");
#endif
  //build the message
  char temp[6]; //2 int, 2 dec, 1 point, and \0
  char hum[6];

  // check if returns are valid, if they are NaN (not a number) then something went wrong!
  char buffer[40];
  if (isnan(t) || isnan(h) || (h == 0.0)) {
    sprintf(buffer, "ID:%d:TS:%lu:ER:ERROR\0", this_node, millis()); //millis provides a stamp for deduping if signal is repeated
    Serial.print("Failed to read from DHT: ");
    Serial.println(buffer);
    t = -1; f = -1; h = -1;
  } else {
    if (UNIT == 0) {  //choose the right unit F or C
      ftoa(temp, f);
    } else {
      ftoa(temp, t);
    }
    ftoa(hum, h);
  }
  Serial.flush();

  // send temperature message
  StaticJsonBuffer<50> jsonBuffer1;
  JsonObject& message1 = jsonBuffer1.createObject();
  message1["id"] = this_node;
  if (isnan(t))
    message1["tf"] = -1;
  else message1["tf"] = f;
  memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
  message1.printTo(buffer, sizeof(buffer));
  sendMessage(buffer, 'S');

  // send humidity message
  StaticJsonBuffer<50> jsonBuffer2;
  JsonObject& message2 = jsonBuffer2.createObject();
  message2["id"] = this_node;
  if (isnan(h) || (h == 0.0)) {
    message2["rh"] = -1;
    message2["err"] = errorCode;
  }
  else message2["rh"] = h;
  memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
  message2.printTo(buffer, sizeof(buffer));
  sendMessage(buffer, 'S');

  if (PIR_PIN > 0) {
    Serial.print("md="); Serial.print(motionDetected); Serial.print(" pir="); Serial.println(pir);
    Serial.flush();
    if (motionDetected != pir) {
      StaticJsonBuffer<50> jsonBuffer3;
      // send pir message
      JsonObject& message3 = jsonBuffer3.createObject();
      message3["id"] = this_node;
      message3["pir"] =  pir ? "True" : "False";
      memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
      message3.printTo(buffer, sizeof(buffer));
      sendMessage(buffer, 'S');
      motionDetected = pir;
    }
  }
  if (BATTERY_PIN > 0) {
    StaticJsonBuffer<50> jsonBuffer4;
    // send battery status message
    JsonObject& message4 = jsonBuffer4.createObject();
    message4["id"] = this_node;
    message4["bs"] =  50.0;
    memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
    message4.printTo(buffer, sizeof(buffer));
    sendMessage(buffer, 'S');
  }
  Serial.flush();
  // max sleep time is 65535, so use loop if interval is larger
  int32_t wait = config_interval * 1000L;
  while (wait > 0) {
    //Serial.print("Waiting ");Serial.print(wait);Serial.println(" ms...");
    //Serial.flush();
    Sleepy::loseSomeTime((wait > 60000) ? 60000 : wait);
    wait = wait - 60000;
  }
}

// https://gist.github.com/bryanthompson/ef4ecf24ad36410f077b
