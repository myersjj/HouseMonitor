#include <SPI.h>
#include <RF24.h>
#include <printf.h>
//#include <ArduinoJson.h>

#define LED 4
#define RF_CS 9
#define RF_CSN 10

#define I2C_ADDRESS 0x04

RF24 radio(RF_CS, RF_CSN);
const uint64_t pipes[2] = { 0xABCDABCD71LL, 0xc2c2c2c2c2LL }; // opposite of sensor

char message[128] = { 0 };

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
  printf_begin();
  printf("RF24 receiver starting...\n\r");
  pinMode(LED, OUTPUT);
  blink(1);
  randomSeed (analogRead(A0));  //initialize the random number generator with
                                //a random read from an unused and floating analog port
  radio.begin();
  // enable dynamic payloads
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setAutoAck(true);

  // optionally, increase the delay between retries & # of retries
  //radio.setRetries(5,15);
  radio.openWritingPipe(pipes[0]);
  radio.openReadingPipe(1, pipes[1]);
  radio.startListening();
  //radio.printDetails();
}


void loop() {
	blink(2);
	    uint8_t rx_data[36];  // we'll receive a packet
            message[128] = { 0 };
            //StaticJsonBuffer<200> jsonBuffer;
            unsigned long started_waiting_at = micros(); 
            uint8_t len = 0;
            uint8_t bytesReceived = 0;
            uint8_t msgLength = 0;
            boolean timeout = false;
            while ( ! radio.available() ){                             // While nothing is received
              if (micros() - started_waiting_at > 200000 ){            // If waited longer than 200ms, indicate timeout and exit while loop
                timeout = true;
                break;
              }
            }
            if ( timeout ){                                             // Describe the results
              Serial.println(F("Failed, read timed out."));
              delay(10000);
              return;
            } else
            {
                unsigned long got_time;                                 // Grab the response, compare, and send to debugging spew
                // Grab the response
                boolean msgComplete = false;
                radio.setPayloadSize(32);
                bytesReceived = 0;
                message[0] = 0;
                msgLength = 0;
                while (!msgComplete) {
                  len = radio.getDynamicPayloadSize();     
                  //Serial.print("Got len="); Serial.println(len); 
                  
                  // If a corrupt dynamic payload is received, it will be flushed
                  if(!len){
                    delay(10000);
                    return; 
                  }
                  radio.read( &rx_data[0], len );
                  //Serial.println("Read data...");
                  //Serial.print("First char="); Serial.println((char)rx_data[0]);
                  // Put a zero at the end for easy printing
                  rx_data[len] = 0;
                  if (len == 1 and rx_data[0] == 0x00)
                  { 
                      //Serial.println("Got terminator...");
                      msgComplete = true;
                  }
                  
                  // assemble message
                  memcpy(&message[msgLength], rx_data, len);
                  msgLength += len;
                  bytesReceived += len;
                } 
            }
            //Serial.print("Message in="); Serial.println(msgLength);
            Serial.println(message);  // sends json data to the pi for handling
#if 0
            JsonObject& root = jsonBuffer.parseObject((char*)&message); // destroys message it appears
            if (!root.success())
            {
              Serial.println("parseObject() failed");
              delay(10000);
              return;
            }
#endif
	delay(10000);
}
