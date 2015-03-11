import RPi.GPIO as GPIO  
import time  
GREEN_LED = 7
RED_LED = 11
# blinking function  
def blink(pin):  
        GPIO.output(pin, GPIO.HIGH)  
        time.sleep(1)  
        GPIO.output(pin, GPIO.LOW)  
        time.sleep(1)  
        return  
# to use Raspberry Pi board pin numbers  
GPIO.setmode(GPIO.BOARD)  
# set up GPIO output channel  
GPIO.setup(GREEN_LED, GPIO.OUT)  
GPIO.setup(RED_LED, GPIO.OUT)  
# blink GPIO17 50 times  
for i in range(0, 10):  
        blink(GREEN_LED)  
        blink(RED_LED)  
GPIO.cleanup()  
