/* 
	Editor: http://www.visualmicro.com
	        visual micro and the arduino ide ignore this code during compilation. this code is automatically maintained by visualmicro, manual changes to this file will be overwritten
	        the contents of the Visual Micro sketch sub folder can be deleted prior to publishing a project
	        all non-arduino files created by visual micro and all visual studio project or solution files can be freely deleted and are not required to compile a sketch (do not delete your own code!).
	        note: debugger breakpoints are stored in '.sln' or '.asln' files, knowledge of last uploaded breakpoints is stored in the upload.vmps.xml file. Both files are required to continue a previous debug session without needing to compile and upload again
	
	Hardware: ATmega328 (Internal 8MHz Clock ArduinoISP), Platform=avr, Package=Breadboard
*/

#define __AVR_ATmega328p__
#define __AVR_ATmega328P__
#define ARDUINO 164
#define ARDUINO_MAIN
#define F_CPU 8000000L
#define __AVR__
#define F_CPU 8000000L
#define ARDUINO 164
#define ARDUINO_ATMEGA328P
#define ARDUINO_ARCH_AVR
extern "C" void __cxa_pure_virtual() {;}

void blink(int n);
void oled_log(const char* s, uint8_t line, uint8_t x, bool clear, bool update);
long readVcc();
long ReadVccxx();
long readVcc();
boolean sendMessage(uint8_t* buffer, uint8_t bufsize, uint8_t msgtype);
//
void doInit();
void doConfig();
void doConfigRetry();
void doConfigWait();
void doWaitAck();
int ftoa(char *a, float f);
void updateTOD();
//
void doSleep();
void checkNetwork();
void doReadSensors();

#include "C:\Users\myersjj\Documents\Arduino\hardware\Breadboard\avr\variants\standard\pins_arduino.h" 
#include "C:\Users\myersjj\Documents\Arduino\hardware\Breadboard\avr\cores\arduino\arduino.h"
#include <HouseSensorRH.ino>
#include <DebugUtils.h>
