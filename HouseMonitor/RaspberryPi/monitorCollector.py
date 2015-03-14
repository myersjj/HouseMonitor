#! /usr/bin/env python
import threading
import os
import sys
import traceback
import time
import random
import datetime
import logging
import json
import serial
from monitorRecord import monitorRecord, monitorStatus
from Queue import Queue

queue = Queue(10)

# Create mapping from transmitter ID number to names. This could be a DB
# some day
DEV = {'1': 'Jim office', '2': 'Front porch'}
motionDetected = False
motionTicks = 4
tickCounter = -1
ser = None

shutdown_event = threading.Event()
status_event = threading.Event()

# create logger with 'spam_application'
logger = logging.getLogger('collector')
logger.setLevel(logging.DEBUG)

# create file handler which logs even debug messages
fh = logging.FileHandler('house_monitor.log')
fh.setLevel(logging.DEBUG)
# create console handler with a higher log level
# ch = logging.StreamHandler()
# ch.setLevel(logging.ERROR)
# create formatter and add it to the handlers
formatter = logging.Formatter(
    '%(asctime)s - %(name)s - %(levelname)s - %(message)s')
fh.setFormatter(formatter)
# ch.setFormatter(formatter)
# add the handlers to the logger
logger.addHandler(fh)
# logger.addHandler(ch)

# simulate Arduino read data from RF receiver
# write message to queue


class ArduinoThread(threading.Thread):

    def run(self):
        global queue, tickCounter, stopThread, motionTicks, motionDetected, ser

        try:
            while True:
                if shutdown_event.is_set():
                    print 'Producer stopping...'
                    queue.put({"type": "shutdown"})
                    return
                # read sensors
                sensor_data = ser.readline()
                # sensor_data = {"id" : 1, "type": "env", "ts" : time.time(),
                #               "tempF" : tempF, "rh" : humidity}
                # If something was read from the Serial Port, read and return
                # the line
                if len(sensor_data):
                    if sensor_data[0] == '{':
                        queue.put(sensor_data)
                        logger.info("Produced {}".format(sensor_data))
                    else:  # got some debug info
                        logger.debug(sensor_data)
                if (tickCounter % 8) == 0:
                    status_event.set()
                time.sleep(random.randint(2, 4))
                tickCounter += 1
        except:
            logger.error('Error: %s' % traceback.format_exc())
        finally:
            pass

# receive message with data and write to database


class ConsumerThread(threading.Thread):

    def run(self):
        global queue, stopThread, ser

        monitorRecord(None, init=True)
        try:
            while True:
                sensor_data = queue.get()  # blocks until item available
                if shutdown_event.is_set():
                    print 'Consumer stopping...'
                    return
                if status_event.is_set():
                    status = monitorStatus()
                    count = ser.write(status)  # write status for lcd output
                    logger.debug(
                        'Wrote %d of %d bytes of status.' % (count, len(status)))
                    if count != len(status):
                        logger.error("Failed to write status.")
                    ser.write('\n')
                    status_event.clear()
                    continue
                monitorRecord(sensor_data)
                logger.debug("Consumed {}".format(sensor_data))
        except:
            logger.error('Error: %s' % traceback.format_exc())
        finally:
            monitorRecord(None, term=True)


def main():
    global ser
    # Open the serial Port
    ser = serial.Serial('/dev/ttyAMA0', 57600, timeout=10)
    ser.flushInput()  # Clear the input buffer
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
