#! /usr/bin/env python
import threading
import os
import sys
import traceback
import time
import random
import datetime
import logging
import serial
from monitorRecord import monitorRecord, monitorStatus, getConfig
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


class ReceiverThread(threading.Thread):

    def run(self):
        global queue, tickCounter, stopThread, motionTicks, motionDetected, ser

        try:
            while True:
                if shutdown_event.is_set():
                    print 'Producer stopping...'
                    # wakes up consumer thread
                    queue.put({"id": 0, "type": "shutdown"})
                    return
                # read sensors
                while True:
                    if shutdown_event.is_set():
                        print 'Producer stopping...'
                        # wakes up consumer thread
                        queue.put({"id": 0, "type": "shutdown"})
                        return
                    try:
                        sensor_data = ser.readline()
                    except:
                        break
                    if not sensor_data:
                        break
                    # sensor_data = {"id" : 1, "type": "env", "ts" : time.time(),
                    #               "tempF" : tempF, "rh" : humidity}
                    # If something was read from the Serial Port, read and return
                    # the line
                    if len(sensor_data):
                        if sensor_data[0] == '{':
                            queue.put(sensor_data)
                            logger.info("Produced {}".format(sensor_data))
                        elif sensor_data[0] == '[':
                            getConfig(ser, sensor_data)
                        else:  # got some debug info
                            logger.debug(sensor_data)
                if (tickCounter % 8) == 0:
                    status_event.set()
                time.sleep(random.randint(2, 4))
                tickCounter += 1
        except:
            logger.error('Receiver error: %s' % traceback.format_exc())
        finally:
            pass

# receive message with data and write to database


class ConsumerThread(threading.Thread):

    def run(self):
        global queue, stopThread, ser

        monitorRecord(ser, None, init=True)
        try:
            while True:
                if shutdown_event.is_set():
                    print 'Consumer stopping...'
                    return
                sensor_data = queue.get()  # blocks until item available
                if status_event.is_set():
                    status = monitorStatus(ser)
                    if status:
                        # write status for lcd output
                        count = ser.write(status)
                        logger.debug(
                            'Wrote %d of %d bytes of status.' % (count, len(status)))
                        if count != len(status):
                            logger.error("Failed to write status.")
                        ser.write('\n')
                    status_event.clear()
                monitorRecord(ser, sensor_data)
                logger.debug("Consumed {}".format(sensor_data))
        except:
            logger.error('Consumer error: %s' % traceback.format_exc())
        finally:
            monitorRecord(ser, None, term=True)


def main():
    global ser
    # Open the serial Port
    ser = serial.Serial('/dev/ttyAMA0', 57600, timeout=10)
    ser.flushInput()  # Clear the input buffer
    ReceiverThread().start()
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
