#include <SPI.h>
#include <RF24.h>
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
const uint64_t pipes[2] = { 0xc2c2c2c2c2LL, 0xABCDABCD71LL };  // opposite in receiver
DHT dht(DHT_PIN, DHTTYPE);

//ISR(WDT_vect) {
//	Sleepy::watchdogEvent();
//} // Setup the watchdog

void setup() {
	Serial.begin(9600);
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
	// enable dynamic payloads
	radio.enableDynamicPayloads();

	// optionally, increase the delay between retries & # of retries
	radio.setRetries(5,15);
	radio.openWritingPipe(pipes[0]);
	radio.openReadingPipe(1, pipes[1]);
	radio.startListening();
	radio.printDetails();
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

void loop() {
	unsigned long time = millis();

	uint8_t data[32];  // we'll transmit a 32 byte packet
	data[0] = 99;    // our first byte in the packet will just be the number 99.

	// get sensor inputs, then sleep 30 seconds
	uint8_t pir = -1;
	if (PIR_PIN > 0) {
		pir = digitalRead(PIR_PIN);
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
	char message[32];
	char buffer[200];
	if (isnan(t) || isnan(h)) {
		sprintf(message, "ID:%d:TS:%lu:ER:ERROR\0", MYID, millis()); //millis provides a stamp for deduping if signal is repeated
		Serial.println("Failed to read from DHT");
		Serial.println(message);
	} else {
		StaticJsonBuffer<200> jsonBuffer;

		JsonObject& message = jsonBuffer.createObject();
		message["id"] = MYID;
		message["temp"] = f;
		message["ts"] = millis();
		message["rh"] = h;
		message["pir"] =  pir;
		//message.printTo(Serial);
		message.printTo(buffer, sizeof(buffer));
		Serial.println();
		//Serial.print("buffer="); Serial.println(buffer);
	}

	// transmit the data
	int remaining = strlen(buffer);
	Serial.print("\nTransmitting..."); Serial.println(strlen(buffer));
	radio.stopListening();
	char brace[1];
	brace[0] = '{';
	//radio.setPayloadSize(1);
	//radio.write(&brace[0], 1);  // initial character that seems to get lost
	Serial.println(buffer);
	// RF24 only writes 32 bytes max in each payload, so split it up if needed
	int sent = 0;
	bool sendOK;
	while (sent < remaining) {
		Serial.print("Sending packet..."); Serial.println(min(32, remaining-sent));
		//Serial.print("First char="); Serial.println(&buffer[sent]);
		radio.setPayloadSize(min(32, remaining-sent));
		sendOK = radio.write(&buffer[sent], min(32, remaining-sent));
		sent += 32;
		if (!sendOK) {
			Serial.println("Write buffer failed");
			break;
		}
	}
	if (sendOK) {
		Serial.println("Sending terminator");
		char term[32];
		term[0] = 0x00;
		//radio.setPayloadSize(1);
		sendOK = radio.write(&term[0], 1);  // termination character
		if (!sendOK) {
			Serial.println("Write terminator failed");
		}
	}
	//delay(10000);               // wait for a second
	Sleepy::loseSomeTime(SLEEP_TIME);
}

// https://gist.github.com/bryanthompson/ef4ecf24ad36410f077b
