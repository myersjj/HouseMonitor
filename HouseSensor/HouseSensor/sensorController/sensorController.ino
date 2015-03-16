// House sensor sketch - James J Myers
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <printf.h>
#include <JeeLib.h>
#include <DHT.h>
#include <ArduinoJson.h>

#define SLEEP_TIME 30000
// Pin 13 has an LED connected on most Arduino boards.
#define LED 13
#define RF_CS 9
#define RF_CSN 10
#define DHT_PIN 2
#define DHTTYPE DHT22   // DHT 22  (AM2302)
// Connect pin 1 (on the left) of the sensor to +5V
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor
#define PIR_PIN 4  // use 0 if no PIR on this sensor
#define UNIT 0      // 0 for Fahrenheit and 1 for Celsius
#define MYID 2      //the ID number of this board.  Change this for each board you flash.
//The ID will be transmitted with the data so you can tell which device is transmitting

RF24 radio(RF_CS, RF_CSN);
RF24Network network(radio);          // Network uses that radio
const uint16_t this_node = 01;        // Address of our node in Octal format
const uint16_t base_node = 00;       // Address of the other node in Octal format

DHT dht(DHT_PIN, DHTTYPE);

bool motionDetected = false;

//ISR(WDT_vect) {
//	Sleepy::watchdogEvent();
//} // Setup the watchdog

void setup() {
	Serial.begin(19200);
	printf_begin();
	pinMode(LED, OUTPUT);
	if (PIR_PIN > 0) {
		pinMode(PIR_PIN, INPUT);
	}
	Serial.println("Starting...");
	digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
	delay(1000);               // wait for a second
	digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
	randomSeed (analogRead(A0));  //initialize the random number generator with
								  //a random read from an unused and floating analog port

	//Initialize the Sensor
	dht.begin();
	Serial.println("Starting radio...");
	radio.begin();
	network.begin(/*channel*/ 90, /*node address*/ this_node);
	digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
	delay(5000);               // wait for a second
	digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
	Serial.println("Ending setup...");
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

boolean sendMessage(char *buffer) {
	// transmit the data
	int remaining = strlen(buffer);
	//Serial.print("\nTransmitting..."); Serial.println(remaining);
	Serial.flush();
	//radio.stopListening();
	// RF24 only writes 32 bytes max in each payload, 8 byte header and 24 data, so split it up if needed
	int sent = 0;
	bool sendOK;
	while (sent < remaining) {
		Serial.print("Sending packet..."); Serial.println(min(32, remaining-sent));
		RF24NetworkHeader header(/*to node*/ base_node, /*type*/ 'S');
		//Serial.print("buflen=");Serial.println(strlen(buffer));
		sendOK = network.write(header, buffer, remaining);
		sent += 32;
		if (!sendOK) {
			Serial.println("Write buffer failed");
			break;
		}
	}
	return sendOK;
}

void loop() {
	uint8_t data[32];  // we'll transmit a 32 byte packet
	data[0] = 99;    // our first byte in the packet will just be the number 99.

	network.update();                          // Check the network regularly

	// get sensor inputs, then sleep 30 seconds
	bool pir = false;
	if (PIR_PIN > 0) {
		uint8_t pirv = digitalRead(PIR_PIN);
		pir = (pirv == 1) ? true : false;
	}

	// Reading temperature or humidity takes about 250 milliseconds!
	// Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
	Serial.println("Reading DHT...");
	float h = dht.readHumidity();
	float t = dht.readTemperature();
	float f = dht.readTemperature(true);

	//build the message
	char temp[6]; //2 int, 2 dec, 1 point, and \0
	char hum[6];
	if (UNIT == 0) {  //choose the right unit F or C
		ftoa(temp, f);
	} else {
		ftoa(temp, t);
	}
	ftoa(hum, h);
	Serial.print("Humidity: ");
	Serial.print(h);
	Serial.print(" %\t");
	Serial.print("Temperature: ");
	Serial.print(t);
	Serial.print(" *C ");
	Serial.print(f);
	Serial.println(" *F\t");

	// check if returns are valid, if they are NaN (not a number) then something went wrong!
	char buffer[25];
	if (isnan(t) || isnan(h)) {
		sprintf(buffer, "ID:%d:TS:%lu:ER:ERROR\0", MYID, millis()); //millis provides a stamp for deduping if signal is repeated
		Serial.println("Failed to read from DHT");
		Serial.println(buffer);
		return;
	}

	// send temperature message
	StaticJsonBuffer<50> jsonBuffer1;
	JsonObject& message1 = jsonBuffer1.createObject();
	message1["id"] = MYID;
	message1["tf"] = f;
	memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
	message1.printTo(buffer, sizeof(buffer));
	Serial.print("buffer="); Serial.print(buffer);Serial.println("...");
	sendMessage(buffer);

	// send humidity message
	StaticJsonBuffer<50> jsonBuffer2;
	JsonObject& message2 = jsonBuffer2.createObject();
	message2["id"] = MYID;
	message2["rh"] = h;
	memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
	message2.printTo(buffer, sizeof(buffer));
	Serial.print("buffer="); Serial.print(buffer);Serial.println("...");
	sendMessage(buffer);

	if (PIR_PIN > 0) {
		//Serial.print("md=");Serial.print(motionDetected);Serial.print(" pir=");Serial.println(pir);
		if (motionDetected != pir) {
			StaticJsonBuffer<50> jsonBuffer3;
			// send pir message
			JsonObject& message3 = jsonBuffer3.createObject();
			message3["id"] = MYID;
			message3["pir"] =  pir;
			memset(buffer, 0, sizeof(buffer));  // clear buffer to avoid reuse
			message3.printTo(buffer, sizeof(buffer));
			Serial.print("buffer="); Serial.print(buffer);Serial.println("...");
			sendMessage(buffer);
			motionDetected = pir;
		}
	}

	Sleepy::loseSomeTime(SLEEP_TIME);
}

// https://gist.github.com/bryanthompson/ef4ecf24ad36410f077b
