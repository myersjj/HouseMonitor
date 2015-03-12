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
import subprocess
import sqlite3 as lite
import twitterwrapper

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


def open_db(db_name):
    """Connect to SQLite database."""
    cur = None
    try:
        con = lite.connect(db_name)
        cur = con.cursor()
    except:
        logger.error('Unable to open database.')
        sys.exit(1)

    # check if temp/humidity table exists
    try:
        cur.execute("SELECT * FROM Monitor where id=1")
        # rows = cur.fetchall()
    except:
        # initialize the new table
        createTable = "CREATE TABLE Monitor(Id INTEGER PRIMARY KEY, unix_time bigint, Date TEXT, Time TEXT, TempC REAL, TempF REAL, Humidity REAL"
        createTable += ", TempFMin REAL, TempFMax REAL, HumidityMin REAL, HumidityMax REAL"
        createTable += ", CurrentHour INT, ReadingsInHour INT, SumTempForHour REAL, AvgTempForHour REAL"
        createTable += ", SumHumidityForHour REAL, AvgHumidityForHour REAL"

        # add 48 columns for temp/humidity per hour values
        for hour in range(0, 24):
            createTable += ', TempHour%s REAL' % hour
            createTable += ', HumidityHour%s REAL' % hour
        createTable += ", LocationId INT, LocationName TEXT"
        createTable += ");"
        cur.execute(createTable)
        con.commit()
    # check if motion table exists
    try:
        cur.execute("SELECT * FROM Motion where id=1")
        # rows = cur.fetchall()
    except:
        # initialize the new table
        createTable = "CREATE TABLE Motion(Id INTEGER PRIMARY KEY, unix_time bigint, Date TEXT, Time TEXT,"
        createTable += " Status INT"
        createTable += ", LocationId INT, LocationName TEXT"
        createTable += ");"
        cur.execute(createTable)
        con.commit()
    try:
        cur.execute("SELECT * FROM Location where id=1")
        # rows = cur.fetchall()
    except:
        # initialize the new table
        createTable = "CREATE TABLE Location(Id INTEGER PRIMARY KEY, LocationName TEXT"
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


def recordEnvData(con, today, sensor_data):
    global tempWarning, tempWarningIssued, humidityWarning, humidityWarningIssued
    global TempHigh, HumidityHigh
    cur = con.cursor()
    d = datetime.datetime.now()
    recordMotionData(con, d, sensor_data)
    thisHour = d.time().hour
    sensorId = sensor_data['id']
    #sensorTS = sensor_data['ts']
    tempF = sensor_data['temp']
    humidity = sensor_data['rh']
    # tempF = 9.0/5.0*temp + 32
    logger.info('{0} Temperature: {1:0.1f} f {2:0.1f} %'.format(
        locationName, tempF, humidity))
    # if temp too  high or humidity too low turn on red led, else turn on green
    tempWarning = ((tempF > TempHigh) or (tempF < TempLow))
    humidityWarning = ((humidity < HumidityLow) or (humidity > HumidityHigh))
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

    # get last row
    cur.execute("SELECT ID FROM Monitor ORDER BY id DESC LIMIT 1;")
    row = cur.fetchone()
    if row == None:
        lastRowNum = 0
        lastDate = ''
        lastRow = [0, d.date().strftime('%Y-%m-%d').lstrip('0'), d.time(), ]
    else:
        lastRowNum = row[0]
        logger.debug("The Id of the last row is %d" % lastRowNum)
        cur.execute("SELECT * FROM Monitor WHERE Id=:Id",
                    {"Id": lastRowNum})
        lastRow = cur.fetchone()
        lastDate = lastRow[2]

    # if hour changed, update summary column for that hour
    if lastDate:
        # update rolling summary data
        currentHour = int(lastRow[11])
        if int(thisHour) != currentHour:
            # save averages for last hour in its cell
            # cell 16 for hour 0 temp, cell 17 for hour 0 humidity etc
            sqlcmd = "UPDATE Monitor SET TempHour%s=?, HumidityHour%s=? WHERE ID=?;" % (
                currentHour, currentHour)
            cur.execute(sqlcmd, ('{0:0.1f}'.format(
                lastRow[14]), '{0:0.1f}'.format(lastRow[16]), lastRowNum))

    # if date changed, insert new row
    # logger.debug('lastDate=%s curDate=%s' % (lastDate, d.date().strftime('%m/%d/%Y').lstrip('0')))
    if lastDate != d.date().strftime('%Y-%m-%d').lstrip('0'):
        # Append the data in the spreadsheet, including a timestamp
        try:
            cur.execute("INSERT INTO Monitor (unix_time, Date, Time, TempF, Humidity, TempFMin, TempFMax, HumidityMin, HumidityMax, CurrentHour, ReadingsInHour, SumTempForHour, AvgTempForHour, SumHumidityForHour, AvgHumidityForHour, locationId, locationName) values(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        (int(time.mktime(d.timetuple())), d.date().strftime('%Y-%m-%d').lstrip('0'), str(d.time()),
                         tempF, humidity, tempF, tempF, humidity, humidity,
                         0, 0, 0.0, 0.0, 0.0, 0.0, sensorId, locationName))
            con.commit()
            lastRowNum = cur.lastrowid
            logger.debug('Wrote new row {0}'.format(lastRowNum))
            cur.execute("SELECT * FROM Monitor WHERE ID=?;", (lastRowNum,))
            lastRow = cur.fetchone()
            readingsInHour = 0
            sumTempForHour = 0.0
            avgTempForHour = 0.0
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
        readingsInHour = int(lastRow[12]) + 1
        sumTempForHour = float(lastRow[13]) + tempF
        avgTempForHour = float(lastRow[13]) / readingsInHour
        sumHumForHour = float(lastRow[15]) + humidity
        avgHumForHour = float(lastRow[15]) / readingsInHour
        # save averages for last hour in its cell
        # cell 16 for hour 0 temp, cell 17 for hour 0 humidity etc
        sqlcmd = "UPDATE Monitor SET TempHour%s=?, HumidityHour%s=? WHERE ID=?;" % (
            thisHour, thisHour)
        cur.execute(sqlcmd, ('{0:0.1f}'.format(
            avgTempForHour), '{0:0.1f}'.format(avgHumForHour), lastRowNum))

    # print 'Summary data: %s %s %s %s %s %s' % (lastRow[9], lastRow[10],
    # lastRow[11], lastRow[12], lastRow[13], lastRow[14])
    sqlcmd = "UPDATE Monitor SET CurrentHour=?, ReadingsInHour=?, SumTempForHour=?, AvgTempForHour=?, SumHumidityForHour=?, AvgHumidityForHour=? WHERE ID=?;"
    cur.execute(sqlcmd,
                (thisHour, readingsInHour, '{0:0.1f}'.format(sumTempForHour), '{0:0.1f}'.format(avgTempForHour),
                 '{0:0.1f}'.format(sumHumForHour), '{0:0.1f}'.format(avgHumForHour), lastRowNum))
    con.commit()
    minTemp = min(tempF, float(lastRow[7]))
    maxTemp = max(tempF, float(lastRow[8]))
    minHum = min(humidity, float(lastRow[9]))
    maxHum = max(humidity, float(lastRow[10]))
    # update current row with latest data
    try:
        sqlcmd = "UPDATE Monitor SET unix_time=?, Time=?, TempF=?, Humidity=? WHERE ID=?;"
        cur.execute(sqlcmd,
                    (int(time.mktime(d.timetuple())), str(d.time()), '{0:0.1f}'.format(tempF), '{0:0.1f}'.format(humidity), lastRowNum))
        # logger.debug('lastRow={0}'.format(lastRow))
        minTemp = min(tempF, float(lastRow[7]))
        maxTemp = max(tempF, float(lastRow[8]))
        minHum = min(humidity, float(lastRow[9]))
        maxHum = max(humidity, float(lastRow[10]))
        cur.execute("UPDATE Monitor SET TempFMin=?, TempFMax=?, HumidityMin=?, HumidityMax=? WHERE ID=?;",
                    ('{0:0.1f}'.format(minTemp), '{0:0.1f}'.format(maxTemp), '{0:0.1f}'.format(minHum), '{0:0.1f}'.format(maxHum), lastRowNum))
    except:
        logger.error('Update error, terminating: %s' % traceback.format_exc())
        return
    con.commit()
    # Wait 30 seconds before continuing
    logger.debug('Updated row {0}'.format(lastRowNum))


def recordMotionData(con, d, sensor_data):
    global offTime  # time motion sensor transitioned to off
    global motionDetected

    # if status is off for more than specified time, then turn off lights
    sensorId = sensor_data['id']
    sensorTS = float(sensor_data['ts'])
    try:
        sensorStatus = sensor_data['pir']  # will be string True or False
    except:
        return  # no motion data in record
    if sensorStatus == 'True':
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
    if motionDetected != sensorStatus:
        cur = con.cursor()
        cur.execute("INSERT INTO Motion (unix_time, Date, Time, Status, LocationId) values(?,?,?,?,?)",
                    (sensorTS,
                     d.date().strftime('%Y-%m-%d').lstrip('0'),
                     str(d.time()), sensorStatus, sensorId))
        con.commit()


def monitorRecord(sensor_data, init=False, term=False):
    global con

    if term == True:
        # cleanup
        if con is not None:
            con.close()
        return

    if init == True:
        try:
            if twitter:
                twitterApi.statuses.update(
                    "House recorder starting at %s!" % datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        except:
            pass

        con = open_db(SQL_DB_NAME)
        return

    logger.debug("Received sensor_data: %s" % sensor_data)
    try:
        # Login if necessary.
        if con is None:
            con = open_db(SQL_DB_NAME)
        d = datetime.datetime.now()
        # convert sensor_data to json
        try:
            sensor_json = json.loads(sensor_data)
            #dataType = sensor_json['type']
            recordEnvData(con, d, sensor_json)
        except:
            logger.error("Json error: " + sensor_data)
    except:
        logger.error('monitorRecord error: %s' % traceback.format_exc())
