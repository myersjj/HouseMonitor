#! /usr/bin/env python
import threading
import os, sys, traceback
import time
import random
import datetime
import logging
import json
import serial
import Adafruit_DHT
import RPi.GPIO as GPIO
from monitorRecord import monitorRecord
from Queue import Queue

queue = Queue(10)

# Type of sensor, can be Adafruit_DHT.DHT11, Adafruit_DHT.DHT22, or Adafruit_DHT.AM2302.
DHT_TYPE = Adafruit_DHT.AM2302

# Example of sensor connected to Raspberry Pi pin 23
DHT_PIN = 23
PIR_PIN = 18

# Create mapping from transmitter ID number to names. This could be a DB some day
DEV = {'1':'Jim office', '2':'Front porch'}
motionDetected = False
motionTicks = 4
tickCounter = -1
shutdown_event = threading.Event()

# create logger with 'spam_application'
logger = logging.getLogger('simulator')
logger.setLevel(logging.DEBUG)

# create file handler which logs even debug messages
fh = logging.FileHandler('house_monitor.log')
fh.setLevel(logging.DEBUG)
# create console handler with a higher log level
# ch = logging.StreamHandler()
# ch.setLevel(logging.ERROR)
# create formatter and add it to the handlers
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
fh.setFormatter(formatter)
# ch.setFormatter(formatter)
# add the handlers to the logger
logger.addHandler(fh)
# logger.addHandler(ch)

# simulate Arduino read data from RF receiver by reading serial port
# write message to queue
class ArduinoThread(threading.Thread):
    def run(self):
        global queue, tickCounter, stopThread, motionTicks, motionDetected
        humidity = 0
        tempF = 0
        temp = 0
        ser = serial.Serial('/dev/ttyAMA0', 9600, timeout=0)  # Open the serial Port
        ser.flushInput()
        GPIO.setmode(GPIO.BCM) 
        GPIO.setwarnings(False)
        GPIO.cleanup()
        GPIO.setup(PIR_PIN, GPIO.IN)  # activate input
        try:
            while True:
                if shutdown_event.is_set():
                    print 'Producer stopping...'
                    queue.put({"type" : "shutdown"})
                    return
                # read sensors
                state = ser.readline()
                if len(state):  # If something was read from the Serial Port, read and return the line
                    print state
                else:
                    pass
                if (tickCounter % motionTicks) == 0:
                    # check motion sensor
                    if GPIO.input(PIR_PIN):
                        motionStatus = True
                    else:
                        motionStatus = False
                    if motionStatus != motionDetected:
                        # record change in motion detection
                        logger.debug('motion changed...')
                        motionDetected = motionStatus
                        sensor_data = {"id" : 1, "type": "motion", "ts" : time.time(),
                               "status" : motionStatus}
                        queue.put(sensor_data)
                        logger.info("Produced motion {}".format(sensor_data))
                # Attempt to get sensor reading.
                humidity, temp = Adafruit_DHT.read(DHT_TYPE, DHT_PIN)
                if humidity is None or temp is None:
                    logger.debug('No temp or humidity reading...')
                    time.sleep(2)
                    continue
                tempF = 9.0 / 5.0 * temp + 32    
                sensor_data = {"id" : 1, "type": "env", "ts" : time.time(),
                               "tempF" : tempF, "humidity" : humidity}
                queue.put(sensor_data)
                logger.info("Produced env {}".format(sensor_data))
                time.sleep(random.randint(20, 40))
                tickCounter += 1
        except:
            logger.error('Error: %s' % traceback.format_exc())
        finally:
            pass

# receive message with data and write to database
class ConsumerThread(threading.Thread):
    def run(self):
        global queue, stopThread
        
        monitorRecord(None, init=True)
        try:
            while True:
                sensor_data = queue.get()  # blocks until item available
                if shutdown_event.is_set():
                    print 'Consumer stopping...'
                    return
                monitorRecord(sensor_data)
                logger.debug("Consumed {}".format(sensor_data))
        except:
            logger.error('Error: %s' % traceback.format_exc())
        finally:
            monitorRecord(None, term=True)

def main():
    ArduinoThread().start()
    ConsumerThread().start()
    
    while True:
        for t in threading.enumerate():
            if threading.currentThread() == t:
                continue  # skip main thread
            # print 'wait for consumer/producer...'
            try:
                t.join(timeout=7)
            except (KeyboardInterrupt, SystemExit):
                shutdown_event.set()
                print 'Shutting down ...'
                return
            except:
                logger.error('join error: %s' % traceback.format_exc())
    
if __name__ == '__main__':
    main()
