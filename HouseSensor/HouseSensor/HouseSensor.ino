// House sensor sketch - James J Myers
//#define DEBUG
#include <U8glib.h>
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <printf.h>
#include "DebugUtils.h"
#include <Wire.h>
#include <FiniteStateMachine.h>

#define SENSOR_NANO 1  // Arduino Nano 5v
#define SENSOR_UNO 2   // Arduino Uno 5v
#define SENSOR_MC8 3   // 8 MHz atmega328 3.3v 
#define SENSOR_MINI 4  // 8 MHz Mini Pro 3.3v
#define SENSOR_TRINKET 5  // 12Mhz Pro Trinket 3.3v

#define SENSOR_POWER_USB 00
#define SENSOR_POWER_AA 01 // 2 AA cells - 3.0v
#define SENSOR_POWER_COIN 2 // 1 LIR2032 cell - 3.7v
#define SENSOR_POWER_LIPO 3

#define RADIO_PA_LEVEL RF24_PA_MAX

//#define NODE_ID 023     // Change this_node for each board you deploy.
#define NODE_ID 01      // Change this_node for each board you deploy.

#if NODE_ID == 01
#define SENSOR_MODEL SENSOR_MC8
#define SENSOR_BATTERY SENSOR_POWER_AA
#define RADIO_PA_LEVEL RF24_PA_HIGH
#define BATTERY_SENSE_PIN A1

#elif NODE_ID == 02
#define SENSOR_MODEL SENSOR_MC8
#define PIR_PIN 5
#define SENSOR_BATTERY SENSOR_POWER_USB
#define RADIO_PA_LEVEL RF24_PA_MAX
#define USE_OLED
//#define RADIO_USE_IRQ
//#define BATTERY_SENSE_PIN A1

#elif NODE_ID == 03
#define SENSOR_MODEL SENSOR_MINI
#define USE_OLED
#define PIR_PIN 5
//#define RADIO_USE_IRQ

#elif NODE_ID == 04
#define SENSOR_MODEL SENSOR_TRINKET
#define USE_OLED
#define SENSOR_BATTERY SENSOR_POWER_USB

#elif NODE_ID == 023
#define SENSOR_MODEL SENSOR_MC8
#define USE_OLED
#define DHT22_PIN 4
#endif

// radio declarations
const uint16_t this_node = NODE_ID;        // Address of our node in Octal format
const uint16_t base_node = 00;       // Address of the other node in Octal format
uint32_t lastContact = 0;  // seconds since last contact with mother
uint16_t readings = 0;
#define RF_CS 9
#define RF_CSN 10
#define RF_INT 0   // interrupt 0 uses pin 2
#define RF_IRQ_PIN 2

#ifdef DHT22_PIN
// Connect pin 1 (on the left) of the sensor to +5V
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor
#define DHT_PIN DHT22_PIN
#include <DHT22.h>
#else
#include <HTU21D.h>
//Create an instance of the object
HTU21D myHumidity;
#endif

// Pin 13 has an LED connected on most Arduino boards.
#define LED 13

const char* version = "0.50";

uint32_t time_offset = 0;   // time in seconds when date/time received
uint32_t seconds_since_midnight = 0;
uint16_t day_of_year = 0;
int32_t wait = -1L;
bool configure_sent = false;
bool configured = false;
bool ackReceived = false;

#ifdef USE_OLED
#define OLED_FONT u8g_font_profont11r
#define OLED_HEIGHT 6
#define OLED_WIDTH 20
U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_NO_ACK);	// Display which does not send ACK
char oled_lines[OLED_HEIGHT][OLED_WIDTH + 1];
#endif

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
//This is for sleep mode. It is not really required, as users could just use the number 0 through 10
typedef enum { wdt_16ms = 0, wdt_32ms, wdt_64ms, wdt_128ms, wdt_250ms, wdt_500ms, wdt_1s, wdt_2s, wdt_4s, wdt_8s } wdt_prescalar_e;
#ifdef DHT22_PIN
DHT22 myDHT22(DHT22_PIN);
#endif
bool motionDetected = false;
uint16_t config_interval = 300;
MyObject sensorConfig; //Variable to store custom object read from EEPROM.

// Finite State Machine declarations
//how many states are we cycling through?
const byte NUMBER_OF_SELECTABLE_STATES = 9;
/** this is the definition of the states that our program uses */
State init_s(doInit);
State config_s(doConfig);
State configWait_s(doConfigWait);
State configRetry_s(doConfigRetry);
State readSensors_s(doReadSensors);
State waitAck_s(doWaitAck);
State sleep_s(doSleep);
static unsigned long configStartTime = 0;
static unsigned long configResume = 0;
static unsigned long ackResume = 0;

/** the state machine controls which of the states get attention and execution time */
FSM stateMachine = FSM(init_s); //initialize state machine, start in state: init_s

#ifdef DEBUG
void blink(int n) {
  for (int i = 1; i <= n; i++) {
    digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(1000);               // wait for a bit
    digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
  }
}
#define DEBUG_BLINK(n) \
  blink(n);
#else
#define DEBUG_BLINK(n)
#endif

#ifdef USE_OLED
#define OLED_LOG(s, line, x, clear, update) \
  oled_log(s, line, x, clear, update)
#define OLED_SLEEP() \
  delay(4000);u8g.sleepOn();delay(200)
#define OLED_TOD() \
  updateTOD()
void oled_log(const char* s, uint8_t line, uint8_t x, bool clear, bool update) {
  uint8_t l = min(OLED_WIDTH - x, strlen(s));
  if (clear) {
    memset(oled_lines[line], ' ', OLED_WIDTH);
  }
  memcpy(&oled_lines[line][x], s, l);
  if (!update) return;
  //u8g.setFont(u8g_font_unifontr);
  //u8g.setFont(u8g_font_6x12r);
  u8g.sleepOff();
  u8g.setFont(OLED_FONT);
  uint8_t lineHeight = u8g.getFontAscent() - u8g.getFontDescent();
  u8g.firstPage();
  do {
    for (int i = 1; i <= OLED_HEIGHT; i++) {
      u8g.drawStr( 0, i * lineHeight - 3, oled_lines[i - 1]);
    }
  } while ( u8g.nextPage() );
  //delay(5000); //allow me to see screen
}
#else
#define OLED_LOG(s, line, x, clear, update)
#define OLED_SLEEP()
#define OLED_TOD()
#endif

#ifdef BATTERY_SENSE_PIN
int oldBatteryPcnt = 0;
/* Internal_ref=1.1V, res=10bit=2^10-1=1023
2AA: Vin/Vbat=470e3/(1e6+470e3), 3AA: Vin/Vbat=680e3/(2.5e6+680e3), 4AA: Vin/Vbat=1e6/(1e6+5e6), 
5-6AA: Vin/Vbat=680e3/(5e6+680e3), 6-8AA: Vin/Vbat=1e6/(1e6+10e6)  
Vlim = 1.1*Vbat/Vin = 9.188235 V (Set eventually to Vmax) this is for 6-AA
Volts per bit = Vlim/1023 = 0.008982
*/
#if SENSOR_BATTERY == SENSOR_POWER_AA
#define VMIN 1.5 // (Min input voltage to regulator according to datasheet or guessing. (?) )
#define VMAX 3.0 // (Known or desired voltage of full batteries. If not, set to Vlim.)
#define VLIM 3.13
#define VOLTS_PER_BIT VLIM/1023L
#endif
#if SENSOR_BATTERY == SENSOR_POWER_COIN
#define VMIN 1.5 // (Min input voltage to regulator according to datasheet or guessing. (?) )
#define VMAX 4.0  //  " " " high level. Vmin<Vmax<=4.0
#define VLIM 3.13
#define VOLTS_PER_BIT VLIM/1023L
#endif

long readVcc() {
   int sensorValue = analogRead(BATTERY_SENSE_PIN);    // Battery monitoring reading
//  Serial.println(sensorValue);
  float Vbat  = sensorValue * VOLTS_PER_BIT;
//  Serial.print("Battery Voltage: "); Serial.print(Vbat); Serial.println(" V");
  int batteryPcnt = static_cast<int>(((Vbat-VMIN)/(VMAX-VMIN))*100.);   
//  Serial.print("Battery percent: "); Serial.print(batteryPcnt); Serial.println(" %");
  if (oldBatteryPcnt != batteryPcnt) {
    oldBatteryPcnt = batteryPcnt;
  }  
  return batteryPcnt;
}
#else
long readVcc() {
  return 0L;
}
#endif

boolean sendMessage(char *buffer, char msgtype) {
  // transmit the data
  //Serial.print("\nTransmitting..."); Serial.println(remaining);
  //Serial.flush();
  // RF24 only writes 32 bytes max in each payload, 8 byte header and 24 data
  int bufsize = strlen(buffer);
  buffer[bufsize] = '\0';  // ensure buffer is terminated properly
  RF24NetworkHeader header(/*to node*/ base_node, /*type*/ msgtype);
  DEBUG_PRINT_START("Sending packet to "); DEBUG_PRINT(base_node); DEBUG_PRINT(" at "); DEBUG_PRINT(millis());
  DEBUG_PRINT(" "); DEBUG_PRINT(msgtype); DEBUG_PRINT(":"); DEBUG_PRINTLN(bufsize);
  bool sendOK = network.write(header, buffer, bufsize + 1); // add 1 to include terminator
  if (!sendOK) {
    DEBUG_PRINTLN("Write buffer failed");
    OLED_LOG("Write failed", 1, 0, true, true);
    // signal error
    DEBUG_BLINK(1);
  } else {
    lastContact = millis() / 1000L;
    DEBUG_PRINT_START("sent="); DEBUG_PRINT(buffer); DEBUG_PRINTLN("...");
    OLED_LOG(" ", 1, 0, true, false);
    OLED_LOG(buffer, 3, 0, true, true);
  }
  DEBUG_FLUSH();
  return sendOK;
}

void setup() {

}

void doInit() {
  char buffer[25];
#ifdef DEBUG
  Serial.begin(9600);
#else
  Serial.begin(57600);
#endif
  //printf_begin();
  DEBUG_PRINT_START("RF24 v"); DEBUG_PRINT(version); DEBUG_PRINTLN(" sensor starting...");
#ifdef USE_OLED
  memset(oled_lines, ' ', OLED_HEIGHT * (OLED_WIDTH + 1));
  for (int i = 0; i < OLED_HEIGHT; i++) {
    oled_lines[i][OLED_WIDTH] = '\0';
  }
  // initialize OLED
  if ( u8g.getMode() == U8G_MODE_BW ) {
    u8g.setColorIndex(1);         // pixel on
  }
  sprintf(buffer, "%2d  yyy.hh:mm:ss    ", this_node);
  OLED_LOG(buffer, 0, 0, true, false);
  OLED_LOG("01234567890123456789", OLED_HEIGHT - 1, 0, true, true); // test last line length
#endif
  EEPROM.get( eeAddress, sensorConfig );
  if (!sensorConfig.initialized) {
    sensorConfig.initialized = true;
    sensorConfig.restarts = 0L;
    sensorConfig.sensorId = this_node;
    sensorConfig.interval = config_interval;
  }
  sensorConfig.restarts += 1;
  config_interval = sensorConfig.interval;
  config_interval = (config_interval < 0) ? 300 : config_interval;
  config_interval = (config_interval > 3600) ? 300 : config_interval;
  EEPROM.put( eeAddress, sensorConfig );
#ifndef DHT22_PIN
#ifdef PIR_PIN
  sprintf(buffer, "%s--%4d:i2c,%d", version, config_interval, PIR_PIN);
#else
  sprintf(buffer, "%s--%4d:i2c,%d", version, config_interval, 0);
#endif
#else
#ifdef PIR_PIN
  sprintf(buffer, "%s--%4d:%d,%d", version, config_interval, DHT_PIN, PIR_PIN);
#else
  sprintf(buffer, "%s--%4d:%d,%d", version, config_interval, DHT_PIN, 0);
#endif
#endif
  OLED_LOG(buffer, 4, 0, true, true);
  DEBUG_PRINT_START("restarts="); DEBUG_PRINT(sensorConfig.restarts);
#ifdef DHT22_PIN
  DEBUG_PRINT(" DHT "); DEBUG_PRINT(DHT_PIN);
#else
  myHumidity.begin();
#endif
#ifdef PIR_PIN
  DEBUG_PRINT(" PIR "); DEBUG_PRINT(PIR_PIN);
#endif
#ifdef BATTERY_SENSE_PIN
  DEBUG_PRINT(" bat "); DEBUG_PRINT(BATTERY_SENSE_PIN);
#endif
  DEBUG_PRINT(" int "); DEBUG_PRINTLN(config_interval);
  //pinMode(LED, OUTPUT);
#ifdef PIR_PIN
  //pinMode(PIR_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);
#endif
  //randomSeed (analogRead(A0));  //initialize the random number generator with
  //a random read from an unused and floating analog port

  //Initialize the Radio
  radio.begin();
  radio.setPALevel(RADIO_PA_LEVEL);
#ifdef SENSOR_BATTERY
  analogReference(INTERNAL);    // use the 1.1 V internal reference for battery level measuring
#endif
  network.begin(/*channel*/ 90, /*node address*/ this_node);
  //radio.printDetails();
  sprintf(buffer, "Started RF24 PA=%u", radio.getPALevel());
  OLED_LOG(buffer, 3, 0, true, true);
  network.setup_watchdog(wdt_8s);  // set up watchdog timer for 8 seconds
  delay(2000);  // delay to allow DHT22 to stabilize
#ifdef DHT22_PIN
  sprintf(buffer, "ID:%d DHT:%d PA:%d", this_node, DHT_PIN, radio.getPALevel());
#else
  sprintf(buffer, "ID:%d DHT:i2c PA:%d", this_node, radio.getPALevel());
#endif
  sendMessage(buffer, 'i');
  Serial.flush();
  DEBUG_BLINK(this_node);
  configStartTime = millis();
  stateMachine.transitionTo(config_s);
}

void doConfig() {
  char configMsg[25];
  sprintf(configMsg, "{\"cfg\":1,\"v\":\"%s\"}", version);
  //OLED_LOG(configMsg, 1, 0, true);
  bool sendOK = sendMessage(configMsg, 'c');
  if (sendOK) {
    configure_sent = true;
    configStartTime = millis();
    configResume = configStartTime + 10000;
    stateMachine.transitionTo(configWait_s);
  } else {
    //Serial.println("request config failed...");
    OLED_LOG("config failed", 1, 0, true, true);
    DEBUG_BLINK(1);
    configResume = configStartTime + 60000;
    // TODO: sleep for a minute
    stateMachine.transitionTo(configRetry_s);
  }
}

void doConfigRetry() {
  if (millis() > configResume) {
    configStartTime = millis();
    stateMachine.transitionTo(config_s);
  }
}

void doConfigWait() {
  checkNetwork();
  if (configured) {
    stateMachine.transitionTo(readSensors_s);
  } else {
    if (millis() > configResume) {
      configStartTime = millis();
      stateMachine.transitionTo(config_s);
    }
  }
}

void doWaitAck() {
  checkNetwork();
  if (ackReceived) {
    ackReceived = false;
    stateMachine.transitionTo(sleep_s);
  } else {
    if (millis() > ackResume) {
      stateMachine.transitionTo(sleep_s);
    }
  }
}

int ftoa(char *a, float f) //translates floating point readings into strings to send over the air
{
  int left = int(f);
  float decimal = f - left;
  int right = decimal * 100; //2 decimal points
  sprintf(a, "%d.%02d", left, right);
  return 1;
}
#ifdef USE_OLED
void updateTOD() {
  char looptime[OLED_WIDTH + 1];
  uint32_t ssm = ((millis() / 1000L) - time_offset) + seconds_since_midnight; // seconds since midnight
  uint16_t days = ssm / 86400L;
  ssm = ssm % 86400L;
  uint16_t hh = ssm / 3600L;
  uint16_t mm = (ssm - (hh * 3600L)) / 60;
  uint16_t ss = ssm % 60;
  sprintf(looptime, "%03u.%02u:%02u:%02u    ", day_of_year + days, hh, mm, ss);
  OLED_LOG(looptime, 0, strlen(" 3  "), false, false);
  sprintf(looptime, "%03u in %06u %05u", readings, lastContact, (millis() / 1000L) - lastContact);
  OLED_LOG(looptime, 5, 0, true, true);
}
#endif
void loop() {
  //THIS LINE IS CRITICAL
  //do not remove the stateMachine.update() call, it is what makes this program 'tick'
  stateMachine.update();

  //OLED_TOD();
  //checkNetwork();
}

void doSleep() {
  // # cycles must be <= 127
  int sleepCycles = config_interval/8;
  char sleepMsg[21];
  sprintf(sleepMsg, "Sleep %d seconds", config_interval);
  OLED_LOG(sleepMsg, 1, 0, true, true);
  OLED_TOD();
  OLED_SLEEP();
  radio.stopListening();         // Switch to PTX mode. Payloads will be seen as ACK payloads, and the radio will wake up
  byte adcsra = ADCSRA;          //save the ADC Control and Status Register A
  ADCSRA = 0;                    //disable the ADC
  do {
#ifdef RADIO_USE_IRQ
    bool fullTime = network.sleepNode(min(127, sleepCycles), RF_INT);  // Sleep the node for n cycles of 8 second intervals
#else
    bool fullTime = network.sleepNode(min(127, sleepCycles), 255);  // Sleep the node for n cycles of 8 second intervals
#endif
    if (!fullTime) {
      sprintf(sleepMsg, "Wake RF IRQ");
      OLED_LOG(sleepMsg, 1, 0, true, true);
      break;  // got interrupt so we need to handle it
    } else {
      sleepCycles -= 127;
    }
  } while (sleepCycles > 0);
  ADCSRA = adcsra;               //restore ADCSRA
  stateMachine.transitionTo(readSensors_s);
}

void checkNetwork() {
  network.update();                          // Check the network regularly

  // can only receive 24 bytes of data!!!
  while ( network.available() ) {     // Is there anything ready for us?
    RF24NetworkHeader header;        // If so, grab it and process it
    network.peek(header);
    DEBUG_PRINT_START("header="); DEBUG_PRINTLN((char)header.type);
    uint8_t payload[25];  // we'll receive a packet
    memset(payload, 0, sizeof(payload));  // clear the buffer
    int count = network.read(header, &payload, sizeof(payload) - 1);
    payload[count] = 0;  // terminate buffer
    OLED_LOG((char*)payload, 3, 0, true, true);
    unsigned int interval = 0;
    switch (header.type) {
      case 'c': {
          // parse config json from manager
          StaticJsonBuffer<64> jsonBuffer;
          JsonObject& root = jsonBuffer.parseObject((char*)payload);

          if (!root.success()) {
            Serial.print("parseObject() sensor config failed: ");
            Serial.println((char*)payload);
          } else {
            uint8_t sid = root["id"];
            sid = sid - 1;
            if (root.containsKey("i")) {
              interval = root["i"];
              config_interval = interval >= 30 ? interval : 30;
              config_interval = (config_interval <= 3600) ? config_interval : 300;
              sensorConfig.interval = config_interval;
              EEPROM.put( eeAddress, sensorConfig );
            }
            DEBUG_PRINT_START("interval="); DEBUG_PRINTLN(config_interval);
            if (root.containsKey("t")) {
              seconds_since_midnight = root["t"];
              time_offset = millis() / 1000;
              configured = (day_of_year != 0);
              OLED_TOD();
              ackReceived = true;
            }
            if (root.containsKey("d")) {
              day_of_year = root["d"];
              char doy[10];
              sprintf(doy, "%03d", day_of_year);
              OLED_LOG(doy, 0, strlen(" 3  "), false, true);
              configured = (seconds_since_midnight != 0);
              OLED_TOD();
            }
          }
          break;
        }
      default:
        Serial.print('Invalid msg type:'); Serial.println(header.type);
        break;
    }  //end switch
  }  // end while
  //delay(200);  // let interrupts settle
}

// get sensor inputs
void doReadSensors() {
  readings++;
#ifdef PIR_PIN
  bool pir = false;
  uint8_t pirv = digitalRead(PIR_PIN);
  Serial.print("pirv="); Serial.println(pirv);
  pir = (pirv == 1) ? true : false;
#endif
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
        break;
      }
    case DHT_ERROR_CHECKSUM:
      DEBUG_PRINT_START("check sum error ");
      DEBUG_PRINT(myDHT22.getTemperatureC());
      DEBUG_PRINT("C ");
      DEBUG_PRINT(myDHT22.getHumidity());
      DEBUG_PRINTLN("%");
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
#else
  h = myHumidity.readHumidity();
  f = myHumidity.readTemperature();  // in Celsius
  f = (f * 1.8) + 32.0;
#endif
  //build the message
  char temp[6]; //2 int, 2 dec, 1 point, and \0
  char hum[6];

  // check if returns are valid, if they are NaN (not a number) then something went wrong!
  char buffer[40];
  if (isnan(t) || isnan(h) || (h == 0.0)) {
    sprintf(buffer, "ID:%d:TS:%lu:ER:ERROR", this_node, millis()); //millis provides a stamp for deduping if signal is repeated
    DEBUG_PRINT_START("Failed to read from DHT: ");
    DEBUG_PRINTLN(buffer);
    t = -1; f = -1; h = -1;
  } else {
    ftoa(temp, f);
    ftoa(hum, h);
  }
  Serial.flush();

  // send temperature/humidity message
  StaticJsonBuffer<50> jsonBuffer1;
  JsonObject& message1 = jsonBuffer1.createObject();
  if (isnan(f) || (f > 120.0))
    message1["tf"] = -1;
  else message1["tf"] = f;
  if (isnan(h) || (h == 0.0) || (h > 100.0)) {
    message1["rh"] = -1;
#ifdef DHT22_PIN
    message1["err"] = errorCode;
#endif
  }
  else message1["rh"] = h;
  memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
  message1.printTo(buffer, sizeof(buffer));
  sendMessage(buffer, 'S');
#ifdef USE_OLED
  OLED_LOG(temp, 2, 0, false, false);
  OLED_LOG(hum, 2, strlen("00.00 "), false, true);
#endif
  bool sendMessage3 = false;
  StaticJsonBuffer<50> jsonBuffer3;
  JsonObject& message3 = jsonBuffer3.createObject();
#ifdef PIR_PIN
  Serial.print("md="); Serial.print(motionDetected); Serial.print(" pir="); Serial.println(pir);
  Serial.flush();
  if (motionDetected != pir) {
    // send pir message
    message3["m"] =  pir ? 1 : 0;
    sendMessage3 = true;
    motionDetected = pir;
  }
#ifdef USE_OLED
  char pirstr[4];
  OLED_LOG(itoa(pir, pirstr, 10), 2, strlen("00.00 00.00 "), false, true);
#endif
#endif
#ifdef BATTERY_SENSE_PIN
  // send battery status message
  message3["bs"] =  readVcc();
  sendMessage3 = true;
#endif
  if (sendMessage3) {
    message3["ack"] = 0;  // do not acknowledge with time
    memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
    message3.printTo(buffer, sizeof(buffer));
    sendMessage(buffer, 'S');
  }
  Serial.flush();
  ackResume = millis() + 10 * 1000L;
  stateMachine.transitionTo(waitAck_s);
}

// https://gist.github.com/bryanthompson/ef4ecf24ad36410f077b
