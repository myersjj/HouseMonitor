//This is a automatic generated file
//Please do not modify this file
//If you touch this file your change will be overwritten during the next build
//This file has been generated on 2015-05-09 11:28:53

#include "Arduino.h"
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
#include <DHT22.h>
#include <HTU21D.h>
void oled_log(const char* s, uint8_t line, uint8_t x, bool clear, bool update) ;
long readVcc() ;
boolean sendMessage(char *buffer, char msgtype) ;
void setup() ;
void doInit() ;
void doConfig() ;
void doConfigRetry() ;
void doConfigWait() ;
void doWaitAck() ;
int ftoa(char *a, float f)  ;
void updateTOD() ;
void loop() ;
void doSleep() ;
void checkNetwork() ;
void doReadSensors() ;


#include "HouseSensor.ino"
