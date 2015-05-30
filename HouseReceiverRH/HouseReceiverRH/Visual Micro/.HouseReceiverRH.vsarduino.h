/* 
	Editor: http://www.visualmicro.com
	        visual micro and the arduino ide ignore this code during compilation. this code is automatically maintained by visualmicro, manual changes to this file will be overwritten
	        the contents of the Visual Micro sketch sub folder can be deleted prior to publishing a project
	        all non-arduino files created by visual micro and all visual studio project or solution files can be freely deleted and are not required to compile a sketch (do not delete your own code!).
	        note: debugger breakpoints are stored in '.sln' or '.asln' files, knowledge of last uploaded breakpoints is stored in the upload.vmps.xml file. Both files are required to continue a previous debug session without needing to compile and upload again
	
	Hardware: Arduino Pro or Pro Mini w/ ATmega328 (3.3V, 8 MHz), Platform=avr, Package=arduino
*/

#define __AVR_ATmega328p__
#define __AVR_ATmega328P__
#define ARDUINO 164
#define ARDUINO_MAIN
#define F_CPU 8000000L
#define __AVR__
#define F_CPU 8000000L
#define ARDUINO 164
#define ARDUINO_AVR_PRO
#define ARDUINO_ARCH_AVR
extern "C" void __cxa_pure_virtual() {;}

void blink(int n);
//
void oledClearLine(int line);
void oledPutLine(int line, int column, char* msg, bool clear);
void processConfig(char* msg);
int freeRam ();
void oledUpdate(int sid);
//

#include "C:\Program Files (x86)\Arduino\hardware\arduino\avr\variants\eightanaloginputs\pins_arduino.h" 
#include "C:\Program Files (x86)\Arduino\hardware\arduino\avr\cores\arduino\arduino.h"
#include <HouseReceiverRH.ino>
#include <printf.h>
