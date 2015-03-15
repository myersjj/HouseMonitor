#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <printf.h>

#define LED 4
#define RF_CS 9
#define RF_CSN 10

#define I2C_ADDRESS 0x04

RF24 radio(RF_CS, RF_CSN);
RF24Network network(radio);          // Network uses that radio
const uint16_t this_node = 00;        // Address of our node in Octal format
const uint16_t other_node = 01;       // Address of the other node in Octal format

char message[128] = { 0 };

String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
boolean readMore = false;        // true if serial read incomplete

void blink(int n) {
	for (int i=1; i<=n; i++) {
		digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
		delay(500);               // wait for a bit
		digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
	}
}

void setup()
{
  Serial.begin(57600);  //Start the Serial at 57600 baud
  Serial.flush();
  // reserve 200 bytes for the inputString:
  inputString.reserve(200);
  printf_begin();
  printf("RF24 receiver starting...\n\r");
  pinMode(LED, OUTPUT);
  blink(1);
  randomSeed (analogRead(A0));  //initialize the random number generator with
                                //a random read from an unused and floating analog port
  radio.begin();
  network.begin(/*channel*/ 90, /*node address*/ this_node);
}

void loop() {

  network.update();                  // Check the network regularly

  if (readMore && Serial.available()) {
    //Serial.print("Read more data...");Serial.println(Serial.available());
    while (Serial.available()) {
      // get the new byte:
      char inChar = (char)Serial.read(); 
      // add it to the inputString:
      inputString += inChar;
      // if the incoming character is a newline, set a flag
      // so the main loop can do something about it:
      if (inChar == '\n'|| inputString.length() >= 200) {
        stringComplete = true;
        readMore = false;
        break;
      } 
    }
    //Serial.print("Got more data...");Serial.println(inputString.length());
  }
  if (stringComplete) {
    //Serial.print("echo length=");Serial.println(inputString.length());
    //Serial.print("echo: ");Serial.println(inputString); 
    //Serial.flush();
    // TODO: display on LCD screen
    // clear the string:
    inputString = "";
    stringComplete = false;
  }
  // can only receive 24 bytes of data!!!
  while ( network.available() ) {     // Is there anything ready for us?

      RF24NetworkHeader header;        // If so, grab it and print it out
      network.peek(header);
      uint8_t payload[25];  // we'll receive a packet
      int count = network.read(header,&payload,sizeof(payload));
      payload[count] = 0;  // terminate buffer
      Serial.print("Received "); Serial.println(count);
      Serial.print("From ");Serial.print(header.from_node);
      Serial.print("->");Serial.print(header.to_node);
      Serial.print(" id ");Serial.print(header.id);
      Serial.print(" type ");Serial.println(header.type);
      Serial.println((const char*)&payload);  // sends json data to the pi for handling
      Serial.flush(); // wait for write to complete so we don't overwrite buffer
  }
}

/*
  SerialEvent occurs whenever a new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 Arduino only has 64 byte receive buffer, so may need multiple loops to read whole message.
 */
void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read(); 
    // add it to the inputString:
    inputString += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if ((inChar == '\n') || (inputString.length() >= 200)) {
      stringComplete = true;
      readMore = false;
      break;
    } 
  }
  //Serial.print("eof:" );Serial.println(Serial.available());
  if (!stringComplete) readMore = true;
}

