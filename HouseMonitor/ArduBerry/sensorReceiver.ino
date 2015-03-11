#include <SPI.h>
#include <RF24.h>
#include "printf.h"
#include <ArduinoJson.h>

#define LED 4
#define RF_CS 9
#define RF_CSN 10

RF24 radio(RF_CS, RF_CSN);
const uint64_t pipes[2] = { 0xe7e7e7e7e7LL, 0xc2c2c2c2c2LL };

void blink(int n) {
	for (int i=1; i<=n; i++) {
		digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
		delay(500);               // wait for a bit
		digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
	}
}

void setup()
{
  Serial.begin(9600);  //Start the Serial at 9600 baud
  pinMode(LED, OUTPUT);
  blink(1);
  randomSeed (analogRead(A0));  //initialize the random number generator with
  								  //a random read from an unused and floating analog port
  Serial.println("Starting radio...");
  radio.begin();
  radio.openWritingPipe(pipes[0]);
  radio.openReadingPipe(1, pipes[1]);
  radio.startListening();
  //radio.printDetails();
}

void loop() {
	blink(2);
	if (Serial.read() == 's') //If 's' is received, send the data back
	{
		int sensorValue = analogRead(A0);
		Serial.print((const char *)"Hi");
		Serial.println(sensorValue); //Send 'hi' followed by the analog read value
	}
	blink(3);
	if (radio.available()) {

	    Serial.println("--------------------------------------------------------------------------------");
	    uint8_t rx_data[100];  // we'll receive a packet

	    bool done = false;
	    while (!done) {
	      done = radio.read( &rx_data, sizeof(rx_data) );
	      printf("Got payload @ %lu...\r\n", millis());
	    }

	    // echo it back real fast
	    radio.stopListening();
	    radio.write( &rx_data, sizeof(rx_data) );
	    printf("Sent response.");
	    radio.startListening();

	    // pass json data to the Raspberry Pi
	    Serial.print((const char*)rx_data);

	  }
	delay(10000);
}
