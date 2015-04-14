#!/usr/bin/python

# SQLite for monitor data by Jim Myers (myersjj@gmail.com)
# Adapted from :
# Google Spreadsheet DHT Sensor Data-logging Example

# Depends on the 'gspread' package being installed.  If you have pip installed
# execute:
#   sudo pip install gspread

# Copyright (c) 2014 Adafruit Industries
# Author: Tony DiCola

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
import sys
import traceback
import time
import datetime
import logging
import json
import requests
import subprocess
import RPi.GPIO as GPIO
import sqlite3 as lite
import twitterwrapper
from Adafruit.CharLCD import Adafruit_CharLCD
import Adafruit_SSD1306
from PIL import Image, ImageDraw, ImageFont

TWITTER_KEY = 'wFaa9oYzqjSlFhCvPeTGRoLHb'
TWITTER_SECRET = 'K3hIB3mEP35yveIumudJ2KnLNEAz8bKWTNZFQNPyuasBocMNNW'

# SQLite database info
SQL_DB_NAME = r'house-monitor.db'

# How long to wait (in seconds) between measurements.
FREQUENCY_SECONDS = 30

twitterApi = twitterwrapper.Api()

# create file handler which logs even debug messages
logger = logging.getLogger('recorder')
logger.setLevel(logging.DEBUG)
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

# read and process json config file
config = json.loads(open('monitorHouse.json').read())
twitter = config['twitter']
logger.debug('config=%s' % config)
FREQUENCY_SECONDS = config['tick_interval']
# GREEN_LED = config['green_pin']
# RED_LED = config['red_pin']
locations = config['locations']
monitor_temp = locations[0]['monitor_temp']
monitor_motion = locations[0]['monitor_motion']
TempHigh = monitor_temp['temp_range']['high']
TempLow = monitor_temp['temp_range']['low']
HumidityHigh = monitor_temp['humidity_range']['high']
HumidityLow = monitor_temp['humidity_range']['low']
motionTicks = monitor_motion['ticks']
offDelay = monitor_motion['offDelay']
tickCounter = -1
motionDetected = False
locationId = locations[0]['locationId']
locationName = locations[0]['locationName']
offTime = None

use_databin = config['use_databin']
use_led = config['use_led']
use_oled = config['use_oled']
databin_url = config['databin_url']
databin_id = config['databin_id']
logger.debug('config complete...')


def open_db(db_name):
    """Connect to SQLite database."""
    cur = None
    try:
        con = lite.connect(db_name)
        cur = con.cursor()
    except:
        logger.error('Unable to open database.')
        sys.exit(1)

    try:
        cur.execute("SELECT * FROM Location where id=1")
        # rows = cur.fetchall()
    except:
        # initialize the new table
        createTable = "CREATE TABLE Location(Id INTEGER PRIMARY KEY, LocationName TEXT, status TEXT"
        createTable += ");"
        cur.execute(createTable)
        con.commit()
    # check if temp table exists
    try:
        cur.execute("SELECT * FROM Temperature where id=1")
        # rows = cur.fetchall()
    except:
        # initialize the new table
        createTable = "CREATE TABLE Temperature(Id INTEGER PRIMARY KEY, LocationId INT"
        createTable += ", unix_time bigint, Date TEXT, Time TEXT, TempC REAL, TempF REAL"
        createTable += ", TempFMin REAL, TempFMax REAL"
        createTable += ", CurrentHour INT, ReadingsInHour INT, SumTempForHour REAL, AvgTempForHour REAL"
        # add 24 columns for temp per hour values
        for hour in range(0, 24):
            createTable += ', TempHour%s REAL' % hour
        createTable += ", FOREIGN KEY(locationId) REFERENCES location(id)"
        createTable += ");"
        cur.execute(createTable)
        con.commit()
    # check if humidity table exists
    try:
        cur.execute("SELECT * FROM Humidity where id=1")
        # rows = cur.fetchall()
    except:
        # initialize the new table
        createTable = "CREATE TABLE Humidity(Id INTEGER PRIMARY KEY, LocationId INT"
        createTable += ", unix_time bigint, Date TEXT, Time TEXT, Humidity REAL"
        createTable += ", HumidityMin REAL, HumidityMax REAL"
        createTable += ", CurrentHour INT, ReadingsInHour INT"
        createTable += ", SumHumidityForHour REAL, AvgHumidityForHour REAL"

        # add 24 columns for humidity per hour values
        for hour in range(0, 24):
            createTable += ', HumidityHour%s REAL' % hour
        createTable += ", FOREIGN KEY(locationId) REFERENCES location(id)"
        createTable += ");"
        cur.execute(createTable)
        con.commit()
    # check if motion table exists
    try:
        cur.execute("SELECT * FROM Motion where id=1")
        # rows = cur.fetchall()
    except:
        # initialize the new table
        createTable = "CREATE TABLE Motion(Id INTEGER PRIMARY KEY, LocationId INT"
        createTable += ", unix_time bigint, Date TEXT, Time TEXT, Status INT"
        createTable += ", FOREIGN KEY(locationId) REFERENCES location(id)"
        createTable += ");"
        cur.execute(createTable)
        con.commit()

    # check if battery status exists
    try:
        cur.execute("SELECT * FROM Battery where id=1")
        # rows = cur.fetchall()
    except:
        # initialize the new table
        createTable = "CREATE TABLE Battery(Id INTEGER PRIMARY KEY, LocationId INT"
        createTable += ", unix_time bigint, Date TEXT, Time TEXT, Status REAL"
        createTable += ", FOREIGN KEY(locationId) REFERENCES location(id)"
        createTable += ");"
        cur.execute(createTable)
        con.commit()
    # sys.exit(1)
    return con

print 'Logging sensor measurements to {0} every {1} seconds.'.format(SQL_DB_NAME, FREQUENCY_SECONDS)
logger.info('Office monitor start to %s' % SQL_DB_NAME)
print 'Press Ctrl-C to quit.'

con = None
tempWarning = False
humidityWarning = False
tempWarningIssued = False
humidityWarningIssued = False

if use_led:
    logger.debug("setup LCD")
    # GPIO.cleanup()
    lcd = Adafruit_CharLCD.Adafruit_CharLCD(pins_db=[23, 17, 27, 22])
    lcd.begin(8, 2)
    lcd.clear()
    lcd.message('Startup!')

# 128x32 display with hardware I2C:
if use_oled:
    logger.debug("setup OLED")
    try:
        disp = Adafruit_SSD1306.SSD1306_96_16(18)
        # Initialize library.
        disp.begin()
        # Clear display.
        disp.clear()
        disp.display()
        # Create blank image for drawing.
        # Make sure to create image with mode '1' for 1-bit color.
        width = disp.width
        height = disp.height
        image = Image.new('1', (width, height))
        # Get drawing object to draw on image.
        draw = ImageDraw.Draw(image)
        # Draw a black filled box to clear the image.
        draw.rectangle((0, 0, width, height), outline=0, fill=0)
        # Draw some shapes.
        # First define some constants to allow easy resizing of shapes.
        padding = 2
        shape_width = 20
        top = padding
        bottom = height - padding
        # Move left to right keeping track of the current x position for drawing
        # shapes.
        x = padding
        # Draw an ellipse.
        draw.ellipse((x, top, x + shape_width, bottom), outline=255, fill=0)
        x += shape_width + padding
        # Draw a rectangle.
        draw.rectangle((x, top, x + shape_width, bottom), outline=255, fill=0)
        x += shape_width + padding
        # Draw a triangle.
        draw.polygon([(x, bottom), (x + shape_width / 2, top),
                      (x + shape_width, bottom)], outline=255, fill=0)
        x += shape_width + padding
        # Draw an X.
        draw.line((x, bottom, x + shape_width, top), fill=255)
        draw.line((x, top, x + shape_width, bottom), fill=255)
        x += shape_width + padding
        # Load default font.
        font = ImageFont.load_default()
        # Alternatively load a TTF font.
        # Some other nice fonts to try: http://www.dafont.com/bitmap.php
        #font = ImageFont.truetype('Minecraftia.ttf', 8)
        # Write two lines of text.
        draw.text((x, top), 'Hello', font=font, fill=255)
        draw.text((x, top + 20), 'World!', font=font, fill=255)
        # Display image.
        disp.image(image)
        disp.display()
    except:
        logger.error('OLED error: %s' % traceback.format_exc())


def getConfig(ser, sensor_data):
    """ Send configuration data in JSON format (no more than 24 chars) """
    ser.flushOutput()
    ser.write("Pi Startup!\n")
    if sensor_data == None or sensor_data[1] == 'c' or sensor_data.startswith('[startup]'):
        for location in locations:
            json = '{"id":%d,"i":%d}\n' % (
                location["locationId"], location["interval"])
            ser.write(json)
            logger.info('sent config:%s' % json)
            time.sleep(1)  # small wait to allow handler to read on other side
    elif sensor_data[1] == 's':
        logger.info('status=%s' % sensor_data)
    else:
        logger.error('status=%s' % sensor_data)


def monitorStatus(ser):
    # check if locations transmitting ok
    # Login if necessary.
    return None  # for now...
    global con
    if con is None:
        con = open_db(SQL_DB_NAME)
    con.row_factory = lite.Row  # use dictionaries
    cur = con.cursor()
    d = datetime.datetime.now()
    status = "{"
    # get all locations
    cur.execute("SELECT ID, LocationName, status FROM Location ORDER BY Id;")
    locations = cur.fetchall()
    for location in locations:
        locationId = location["id"]
        currentStatus = location["status"]
        cur.execute("SELECT ID, LocationId, max(unix_time) as utime FROM Monitor WHERE LocationId=:locationId;",
                    {"locationId": locationId})
        row = cur.fetchone()
        if row != None:
            lastReading = row["utime"]
            logger.debug(
                "location {} last reading at {}".format(locationId, lastReading))
            if lastReading == None:
                locationStatus = 'inactive'
            else:
                unixtimeNow = time.mktime(d.timetuple())
                logger.debug('unixtime=%s lastReading=%s' %
                             (unixtimeNow, lastReading))
                logger.debug('time since last reading = %s' %
                             (unixtimeNow - lastReading))  # in seconds
                if (unixtimeNow - lastReading) > 3600:
                    locationStatus = 'dead'
                elif (unixtimeNow - lastReading) > 60:
                    locationStatus = 'late'
                else:
                    locationStatus = 'active'
        else:
            # no status recorded for this location
            locationStatus = 'inactive'
        if currentStatus != locationStatus:
            # update location record with new status
            sqlcmd = "UPDATE Location SET status=? WHERE ID=?;"
            cur.execute(sqlcmd, (locationStatus, locationId))
        status += '"locationId" : {}, "status": {}, '.format(
            locationId, locationStatus)
    status += "}"
    logger.debug('Current Status: %s' % status)
    return status


def recordEnvData(ser, con, today, sensorId, sensor_data):
    """ We get only short json messages with 1 data value (temp or humidity or pir) because of 24 byte
    limit sending RF messages. So we have to just update the one value"""
    global tempWarning, tempWarningIssued, humidityWarning, humidityWarningIssued
    global TempHigh, HumidityHigh

    con.row_factory = lite.Row  # use dictionaries
    cur = con.cursor()
    d = datetime.datetime.now()
    recordMotionData(ser, con, d, sensorId, sensor_data)
    recordBatteryStatus(ser, con, d, sensorId, sensor_data)
    thisHour = d.time().hour
    try:
        tempF = sensor_data['tf']
        if tempF < 0:
            tempF = None
    except:
        tempF = None
    try:
        humidity = sensor_data['rh']
        if humidity < 0:
            humidity = None
    except:
        humidity = None
    if not tempF and not humidity:
        return
    # tempF = 9.0/5.0*temp + 32
    # if temp too  high or humidity too low turn on red led, else turn on green
    tempWarning = tempF and ((tempF > TempHigh) or (tempF < TempLow))
    humidityWarning = humidity and (
        (humidity < HumidityLow) or (humidity > HumidityHigh))
    if tempWarning or humidityWarning:
        if tempWarning and not tempWarningIssued:
            tempWarningIssued = True
            try:
                if tempF > TempHigh:
                    logger.warning(
                        '{0} Temperature too high - {1:0.1f}%!'.format(locationName, tempF))
                    if twitter:
                        twitterApi.statuses.update(
                            "{0} warning: temp too high - {1:0.1f}F!".format(locationName, tempF))
                else:
                    logger.warning(
                        '{0} Temperature too low - {1:0.1f}%!'.format(locationName, tempF))
                    if twitter:
                        twitterApi.statuses.update(
                            "{0} warning: temp too low - {1:0.1f}F!".format(locationName, tempF))
            except:
                pass
        if humidityWarning and not humidityWarningIssued:
            humidityWarningIssued = True
            try:
                if humidity > HumidityHigh:
                    logger.warning(
                        '{0} Humidity too high - {1:0.1f}%!'.format(locationName, humidity))
                    if twitter:
                        twitterApi.statuses.update(
                            "{0} warning: humidity too high - {1:0.1f}%!".format(locationName, humidity))
                else:
                    logger.warning(
                        '{0} Humidity too low - {1:0.1f}%!'.format(locationName, humidity))
                    if twitter:
                        twitterApi.statuses.update(
                            "{0} warning: humidity too low - {1:0.1f}%!".format(locationName, humidity))
            except:
                pass
    else:
        # turn on green
        if tempWarningIssued or humidityWarningIssued:
            logger.info(
                '{0} Temp/humidity is now normal.'.format(locationName))
            try:
                if twitter:
                    twitterApi.statuses.update(
                        "{0} temp/humidity is now normal!".format(locationName))
            except:
                pass
        tempWarningIssued = False
        humidityWarningIssued = False

    # Process temperature if available
    # get last row
    lcdMessage = "%s:F=" % sensorId
    if tempF:
        if use_led:
            lcd.clear()
        lcdMessage += '%s-' % (tempF)

        if use_databin:
            r = requests.get(
                'https://datadrop.wolframcloud.com/api/v1.0/Add?bin={}&loc={}&tempF={}'.format(databin_id, sensorId, tempF))
        cur.execute(
            "SELECT ID FROM Temperature Where LocationId=? ORDER BY id DESC LIMIT 1;", (sensorId,))
        row = cur.fetchone()
        if row == None:
            lastRowNum = 0
            lastDate = ''
            lastRow = [
                0, d.date().strftime('%Y-%m-%d').lstrip('0'), d.time(), ]
        else:
            lastRowNum = row["id"]
            logger.debug("The Id of the last temp row is %d:%d" %
                         (sensorId, lastRowNum))
            cur.execute("SELECT * FROM Temperature WHERE Id=:Id",
                        {"Id": lastRowNum})
            lastRow = cur.fetchone()
            lastDate = lastRow["date"]

        # if hour changed, update summary column for that hour
        if lastDate:
            # update rolling summary data
            currentHour = int(lastRow["currentHour"])
            if int(thisHour) != currentHour:
                # save averages for last hour in its cell
                # cell 16 for hour 0 temp, cell 17 for hour 0 humidity etc
                sqlcmd = "UPDATE Temperature SET TempHour%s=? WHERE ID=?;" % (
                    currentHour)
                cur.execute(sqlcmd, ('{0:0.1f}'.format(
                    lastRow["AvgTempForHour"]), lastRowNum))

        # if date changed, insert new row
        # logger.debug('lastDate=%s curDate=%s' % (lastDate, d.date().strftime('%m/%d/%Y').lstrip('0')))
        if lastDate != d.date().strftime('%Y-%m-%d').lstrip('0'):
            # Append the data in the spreadsheet, including a timestamp
            try:
                cur.execute("INSERT INTO Temperature (unix_time, Date, Time, TempF, TempFMin, TempFMax, CurrentHour, ReadingsInHour, SumTempForHour, AvgTempForHour, locationId) values(?,?,?,?,?,?,?,?,?,?,?)",
                            (int(time.mktime(d.timetuple())), d.date().strftime('%Y-%m-%d').lstrip('0'),
                             str(d.time()), tempF, tempF, tempF,
                             0, 0, 0.0, 0.0, sensorId))
                con.commit()
                lastRowNum = cur.lastrowid
                logger.debug(
                    'Wrote new temp row {0} for loc {1}'.format(lastRowNum, sensorId))
                cur.execute(
                    "SELECT * FROM Temperature WHERE ID=?;", (lastRowNum,))
                lastRow = cur.fetchone()
                readingsInHour = 0
                sumTempForHour = 0.0
                avgTempForHour = 0.0
                # logger.debug('newRow={0}'.format(lastRow))
            except:
                # Error appending data, most likely because credentials are stale.
                # Null out the worksheet so a login is performed at the top of the
                # loop.
                logger.error('Insert error, opening again: %s' %
                             traceback.format_exc())
                cur = None
                time.sleep(FREQUENCY_SECONDS)
                return
        else:
            readingsInHour = int(lastRow["ReadingsInHour"]) + 1
            sumTempForHour = float(lastRow["SumTempForHour"]) + tempF
            avgTempForHour = sumTempForHour / readingsInHour
            # save averages for last hour in its cell
            # cell 16 for hour 0 temp, cell 17 for hour 0 humidity etc
            sqlcmd = "UPDATE Temperature SET TempHour%s=? WHERE ID=?;" % (
                thisHour)
            cur.execute(sqlcmd, ('{0:0.1f}'.format(
                avgTempForHour), lastRowNum))

        # print 'Summary data: %s %s %s %s %s %s' % (lastRow[9], lastRow[10],
        # lastRow[11], lastRow[12], lastRow[13], lastRow[14])
        sqlcmd = "UPDATE Temperature SET CurrentHour=?, ReadingsInHour=?, SumTempForHour=?, AvgTempForHour=? WHERE ID=?;"
        cur.execute(sqlcmd,
                    (thisHour, readingsInHour, '{0:0.1f}'.format(sumTempForHour), '{0:0.1f}'.format(avgTempForHour),
                     lastRowNum))
        con.commit()
        minTemp = min(tempF, float(lastRow["TempFMin"]))
        maxTemp = max(tempF, float(lastRow["TempFMax"]))
        # update current row with latest data
        try:
            sqlcmd = "UPDATE Temperature SET unix_time=?, Time=?, TempF=? WHERE ID=?;"
            cur.execute(sqlcmd,
                        (int(time.mktime(d.timetuple())), str(d.time()), '{0:0.1f}'.format(tempF), lastRowNum))
            # logger.debug('lastRow={0}'.format(lastRow))
            minTemp = min(tempF, float(lastRow["TempFMin"]))
            maxTemp = max(tempF, float(lastRow["TempFMax"]))
            cur.execute("UPDATE Temperature SET TempFMin=?, TempFMax=? WHERE ID=?;",
                        ('{0:0.1f}'.format(minTemp), '{0:0.1f}'.format(maxTemp), lastRowNum))
        except:
            logger.error('Update error, terminating: %s' %
                         traceback.format_exc())
            return
        con.commit()
        # Wait 30 seconds before continuing
        logger.debug('Updated temp row {}:{}'.format(sensorId, lastRowNum))
    else:
        lcdMessage += "n/a\n"

    # Process humidity if available
    # get last row
    lcdMessage += "  h="
    if humidity:
        lcdMessage += '%s' % (humidity)
        if use_databin:
            r = requests.get(
                'https://datadrop.wolframcloud.com/api/v1.0/Add?bin={}&loc={}&humidity={}'.format(databin_id, sensorId, humidity))
        cur.execute(
            "SELECT ID FROM Humidity WHERE LocationId=? ORDER BY id DESC LIMIT 1;", (sensorId,))
        row = cur.fetchone()
        if row == None:
            lastRowNum = 0
            lastDate = ''
            lastRow = [
                0, d.date().strftime('%Y-%m-%d').lstrip('0'), d.time(), ]
        else:
            lastRowNum = row["id"]
            logger.debug("The Id of the last humidity row is %d" % lastRowNum)
            cur.execute("SELECT * FROM Humidity WHERE Id=:Id",
                        {"Id": lastRowNum})
            lastRow = cur.fetchone()
            lastDate = lastRow["date"]

        # if hour changed, update summary column for that hour
        if lastDate:
            # update rolling summary data
            currentHour = int(lastRow["CurrentHour"])
            if int(thisHour) != currentHour:
                # save averages for last hour in its cell
                # cell 16 for hour 0 temp, cell 17 for hour 0 humidity etc
                sqlcmd = "UPDATE Humidity SET HumidityHour%s=? WHERE ID=?;" % (
                    currentHour)
                cur.execute(
                    sqlcmd, ('{0:0.1f}'.format(lastRow["AvgHumidityForHour"]), lastRowNum))

        # if date changed, insert new row
        # logger.debug('lastDate=%s curDate=%s' % (lastDate, d.date().strftime('%m/%d/%Y').lstrip('0')))
        if lastDate != d.date().strftime('%Y-%m-%d').lstrip('0'):
            # Append the data in the spreadsheet, including a timestamp
            try:
                cur.execute("INSERT INTO Humidity (unix_time, Date, Time, Humidity, HumidityMin, HumidityMax, CurrentHour, ReadingsInHour, SumHumidityForHour, AvgHumidityForHour, locationId) values(?,?,?,?,?,?,?,?,?,?,?)",
                            (int(time.mktime(d.timetuple())), d.date().strftime('%Y-%m-%d').lstrip('0'),
                             str(d.time()), humidity, humidity, humidity,
                             0, 0, 0.0, 0.0, sensorId))
                con.commit()
                lastRowNum = cur.lastrowid
                logger.debug('Wrote new humidity row {0}'.format(lastRowNum))
                cur.execute(
                    "SELECT * FROM Humidity WHERE ID=?;", (lastRowNum,))
                lastRow = cur.fetchone()
                readingsInHour = 0
                sumHumForHour = 0.0
                avgHumForHour = 0.0
                # logger.debug('newRow={0}'.format(lastRow))
            except:
                # Error appending data, most likely because credentials are stale.
                # Null out the worksheet so a login is performed at the top of the
                # loop.
                logger.error('Insert error, opening again: %s' %
                             traceback.format_exc())
                cur = None
                time.sleep(FREQUENCY_SECONDS)
                return
        else:
            readingsInHour = int(lastRow["ReadingsInHour"]) + 1
            sumHumForHour = float(lastRow["SumHumidityForHour"]) + humidity
            avgHumForHour = sumHumForHour / readingsInHour
            # save averages for last hour in its cell
            # cell 16 for hour 0 temp, cell 17 for hour 0 humidity etc
            sqlcmd = "UPDATE Humidity SET HumidityHour%s=? WHERE ID=?;" % (
                thisHour)
            cur.execute(sqlcmd, ('{0:0.1f}'.format(avgHumForHour), lastRowNum))

        # print 'Summary data: %s %s %s %s %s %s' % (lastRow[9], lastRow[10],
        # lastRow[11], lastRow[12], lastRow[13], lastRow[14])
        sqlcmd = "UPDATE Humidity SET CurrentHour=?, ReadingsInHour=?, SumHumidityForHour=?, AvgHumidityForHour=? WHERE ID=?;"
        cur.execute(sqlcmd,
                    (thisHour, readingsInHour,
                     '{0:0.1f}'.format(sumHumForHour), '{0:0.1f}'.format(avgHumForHour), lastRowNum))
        con.commit()
        minHum = min(humidity, float(lastRow["HumidityMin"]))
        maxHum = max(humidity, float(lastRow["HumidityMax"]))
        # update current row with latest data
        try:
            sqlcmd = "UPDATE Humidity SET unix_time=?, Time=?, Humidity=? WHERE ID=?;"
            cur.execute(sqlcmd,
                        (int(time.mktime(d.timetuple())), str(d.time()), '{0:0.1f}'.format(humidity), lastRowNum))
            # logger.debug('lastRow={0}'.format(lastRow))
            minHum = min(humidity, float(lastRow["HumidityMin"]))
            maxHum = max(humidity, float(lastRow["HumidityMax"]))
            cur.execute("UPDATE Humidity SET HumidityMin=?, HumidityMax=? WHERE ID=?;",
                        ('{0:0.1f}'.format(minHum), '{0:0.1f}'.format(maxHum), lastRowNum))
        except:
            logger.error('Update error, terminating: %s' %
                         traceback.format_exc())
            return
        con.commit()
        # Wait 30 seconds before continuing
        logger.debug('Updated humidity row {0}'.format(lastRowNum))
    else:
        lcdMessage += "n/a"
    if use_led:
        lcd.message(lcdMessage)
    else:
        ser.write(lcdMessage)
        ser.write('\n')
        logger.debug('oled msg: %s' % lcdMessage)


def recordMotionData(ser, con, d, sensorId, sensor_data):
    global offTime  # time motion sensor transitioned to off
    global motionDetected

    # if status is off for more than specified time, then turn off lights
    try:
        sensorStatus = sensor_data['pir']  # will be string True or False
    except:
        #logger.error('pir error: %s' % traceback.format_exc())
        return  # no motion data in record
    #logger.debug('sensorStatus=%s' % sensorStatus)
    if sensorStatus in ['true', 'True', True]:
        sensorStatus = True
    else:
        sensorStatus = False
    try:
        lightOff = monitor_motion['lightOff']
    except:
        lightOff = None
    if sensorStatus:  # if motion detected
        offTime = None
    else:  # no motion detected this interval
        #  unixtime=1426031992.0 offTime=1426029418.0
        if lightOff:
            if offTime == None:
                # start timing
                offTime = d
            else:  # not first time motion not detected
                unixtimeNow = time.mktime(d.timetuple())
                unixtimeOff = time.mktime(offTime.timetuple())
                logger.debug('unixtime=%s offTime=%s' %
                             (unixtimeNow, unixtimeOff))
                logger.debug('offInterval = %s' %
                             (unixtimeNow - unixtimeOff))  # in seconds
                if (unixtimeNow - unixtimeOff) > (offDelay * 60):
                    # turn off lights
                    logger.info('Turning off light at %s' % d)
                    subprocess.call(
                        'sudo 433Utils/RPi_utils/codesend %s' % lightOff, shell=True)
                    unixtimeOff = unixtimeNow
    #logger.debug('md=%s ss=%s' % (motionDetected, sensorStatus))
    if motionDetected != sensorStatus:
        cur = con.cursor()
        motionDetected = sensorStatus
        #logger.debug('insert into motion...')
        cur.execute("INSERT INTO Motion (unix_time, Date, Time, Status, LocationId) values(?,?,?,?,?)",
                    (int(time.mktime(d.timetuple())),
                     d.date().strftime('%Y-%m-%d').lstrip('0'),
                     str(d.time()), sensorStatus, sensorId))
        #logger.debug('motion insert result=%r' % result)
        con.commit()


def recordBatteryStatus(ser, con, d, sensorId, sensor_data):
    # if status is too lower, send alarm
    try:
        batteryStatus = sensor_data['bs']  # will be string True or False
    except:
        return  # no motion data in record
    cur = con.cursor()
    cur.execute("SELECT ID FROM Battery WHERE locationId=? ORDER BY id DESC LIMIT 1;",
                (sensorId,))
    row = cur.fetchone()
    if row == None:
        cur.execute("INSERT INTO Battery (unix_time, Date, Time, Status, LocationId) values(?,?,?,?,?)",
                    (int(time.mktime(d.timetuple())),
                     d.date().strftime('%Y-%m-%d').lstrip('0'),
                     str(d.time()), 0.0, sensorId))
    cur.execute("SELECT ID FROM Battery WHERE locationId=? ORDER BY id DESC LIMIT 1;",
                (sensorId,))
    row = cur.fetchone()
    lastRowNum = row['id']
    #logger.debug('insert into motion...')
    cur.execute("UPDATE Battery SET unix_time=?, Time=?, Status=? WHERE ID=?",
                (int(time.mktime(d.timetuple())),
                 str(d.time()), batteryStatus, lastRowNum))
    con.commit()
    logger.debug(
        'Updated battery row {} status={}'.format(lastRowNum, batteryStatus))


def monitorRecord(ser, sensor_data, init=False, term=False):
    global con

    if term == True:
        # cleanup
        if con is not None:
            con.close()
        if use_led:
            lcd.clear()
            lcd.message('Shutdown')
        else:
            ser.write('Pi Shutdown!\n')
        GPIO.cleanup()
        return

    if init == True:
        try:
            if twitter:
                twitterApi.statuses.update(
                    "House recorder starting at %s!" % datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        except:
            pass

        con = open_db(SQL_DB_NAME)
        getConfig(ser, None)  # send config data to sensors
        return

    logger.debug("Received sensor_data: %s" % sensor_data)
    try:
        # Login if necessary.
        if con is None:
            con = open_db(SQL_DB_NAME)
        d = datetime.datetime.now()
        # convert sensor_data to json
        try:
            if isinstance(sensor_data, basestring):
                sensor_data = sensor_data.strip()
                sensor_json = json.loads(sensor_data)
            else:
                sensor_json = sensor_data
            sensorId = int(sensor_json['id'])
            recordEnvData(ser, con, d, sensorId, sensor_json)
        except:
            logger.error("Json error: '%s'" % sensor_data)
            logger.error(': %s' % traceback.format_exc())
    except:
        logger.error('monitorRecord error: %s' % traceback.format_exc())
    return None
