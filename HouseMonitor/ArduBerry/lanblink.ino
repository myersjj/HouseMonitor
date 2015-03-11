#include <SPI.h>
#include <Ethernet.h>
#include <sha256.h>
#include <ArduinoJson.h>

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(192,168,1,177);
IPAddress myDns(192,168,1,1);

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client;
uint8_t *hash;

unsigned long lastConnectionTime = 0;          // last time you connected to the server, in milliseconds
boolean lastConnected = false;                 // state of the connection last time through the main loop
const unsigned long postingInterval = 10*1000;  // delay between updates, in milliseconds
boolean connecting = false;

char httpResponse[150];
int charIndex = 0;
bool bodyStarted = false;

int ledPinRed = 3;    // LED connected to digital pin 9
int ledPinGreen = 5;    // LED connected to digital pin 11
int ledPinBlue = 6;    // LED connected to digital pin 12

void setup() {

  // start serial port:
  Serial.begin(9600);

  analogWrite(ledPinRed, 0);
  analogWrite(ledPinGreen, 0);
  analogWrite(ledPinBlue, 0);

    // give the ethernet module time to boot up:
  delay(1000);
  // start the Ethernet connection using a fixed IP address and DNS server:
  Ethernet.begin(mac, ip, myDns);
  // print the Ethernet board/shield's IP address:
  Serial.print("My IP address: ");
  Serial.println(Ethernet.localIP());
}

void loop()
{
  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  if (client.available()) {
    char c = client.read();

    // The JSON parser isn't happy with the surrounding [ and ] brackets, so we'll grab the inner contents of these from the HTTP reponse body
    if (c == ']' && bodyStarted)
    {
      bodyStarted = false;
    }

    if (bodyStarted)
    {
      httpResponse[charIndex] = c;
      charIndex++;
    }

    if (c == '[')
    {
      bodyStarted = true;
    }
  }

  // if there's no net connection, but there was one last time
  // through the loop, then stop the client:
  if (!client.connected() && lastConnected) {
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();

    Serial.println("http response");
    Serial.println(httpResponse);
    // create a JSON buffer, we know it won't exceed 150 bytes
    StaticJsonBuffer<150> jsonBuffer;

    JsonObject& root = jsonBuffer.parseObject(httpResponse);

    if (root.success())
    {
      const char* eventdata = root["EventData"];
      String strEventData = eventdata;

      // Retrieve the RGB colours from the eventdata string
      int s1 = strEventData.indexOf(';');
      int s2 = strEventData.lastIndexOf(';');
      int rColour = strEventData.substring(0,s1).toInt();
      int gColour = strEventData.substring(s1+1,s2).toInt();
      int bColour = strEventData.substring(s2+1,strEventData.length()).toInt();

      analogWrite(ledPinRed, rColour);
      analogWrite(ledPinGreen, gColour);
      analogWrite(ledPinBlue, bColour);

      charIndex = 0;
      // Set boolean back to false so the next HTTP request will be parsed again
      bodyStarted = false;
    }
  }
  // if you're not connected, and ten seconds have passed since
  // your last connection, then connect again and send data:
  if(!client.connected() && (millis() - lastConnectionTime > postingInterval)) {
    Serial.println("making http request");
    httpRequest();
  }
  // store the state of the connection for next time through the loop:
  lastConnected = client.connected();
}

// this method makes a HTTP connection to the server:
void httpRequest() {
  connecting=true;
  // if there's a successful connection:
  if (client.connect("myiotservice.azure-mobile.net", 80)) {
    Serial.println("connecting...");
    // send the HTTP GET request:
    client.println("GET http://myiotservice.azure-mobile.net/tables/sensordata?$top=1&$orderby=__createdAt%20desc HTTP/1.1");
    client.println("Host: http://myiotservice.azure-mobile.net/");

    client.println("Connection: close");
    client.println();

    // note the time that the connection was made:
    lastConnectionTime = millis();
  }
  else {
    // if you couldn't make a connection:
    Serial.println("connection failed");
    Serial.println("disconnecting.");
    client.stop();
  }
}
