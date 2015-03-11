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
import sys, traceback
import time
import datetime
import logging
import json

import Adafruit_DHT
import sqlite3 as lite
import RPi.GPIO as GPIO 
import twitterwrapper

TWITTER_KEY = 'wFaa9oYzqjSlFhCvPeTGRoLHb'
TWITTER_SECRET = 'K3hIB3mEP35yveIumudJ2KnLNEAz8bKWTNZFQNPyuasBocMNNW'

# Type of sensor, can be Adafruit_DHT.DHT11, Adafruit_DHT.DHT22, or Adafruit_DHT.AM2302.
DHT_TYPE = Adafruit_DHT.AM2302

# Example of sensor connected to Raspberry Pi pin 23
DHT_PIN = 23
PIR_PIN = 18
# Example of sensor connected to Beaglebone Black pin P8_11
# DHT_PIN  = 'P8_11'

GREEN_LED = 4
RED_LED = 17

# SQLite database info
SQL_DB_NAME = r'office-monitor.db'

# How long to wait (in seconds) between measurements.
FREQUENCY_SECONDS = 30

# create logger with 'spam_application'
logger = logging.getLogger('monitor')
logger.setLevel(logging.DEBUG)
twitterApi = twitterwrapper.Api()
try:
	new_status = twitterApi.statuses.update("Jim office monitor starting at %s!" % datetime.datetime.now())
except:
	pass

# create file handler which logs even debug messages
fh = logging.FileHandler('monitor.log')
fh.setLevel(logging.DEBUG)
# create console handler with a higher log level
ch = logging.StreamHandler()
ch.setLevel(logging.ERROR)
# create formatter and add it to the handlers
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
fh.setFormatter(formatter)
ch.setFormatter(formatter)
# add the handlers to the logger
logger.addHandler(fh)
logger.addHandler(ch)

# read and process json config file
config = json.loads(open('monitor_office.json').read())
logger.debug('config=%s' % config)
FREQUENCY_SECONDS = config['tick_interval']
GREEN_LED = config['green_pin']
RED_LED = config['red_pin']
monitor_temp = config['monitor_temp']
DHT_PIN = monitor_temp['pin']
TempHigh = monitor_temp['temp_range']['high']
TempLow = monitor_temp['temp_range']['low']
HumidityHigh = monitor_temp['humidity_range']['high']
HumidityLow = monitor_temp['humidity_range']['low']
PIR_PIN = config['monitor_motion']['pin']
motionTicks = config['monitor_motion']['ticks']
tickCounter = -1
motionDetected = False

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
		createTable += ");"
		cur.execute(createTable)
		con.commit()
	# sys.exit(1)
	return con

# blinking function  
def ledOn(pin):  
	GPIO.output(pin, GPIO.HIGH)  
	return  

def ledOff(pin):  
	GPIO.output(pin, GPIO.LOW)  
	return 

def monitorMotion(config, con, today):
	global motionDetected, motionTicks, tickCounter
	
	if tickCounter % motionTicks != 0:
		return
	cur = con.cursor()   
	if GPIO.input(PIR_PIN):
		motionStatus = True
	else:
		motionStatus = False
	if motionStatus != motionDetected:
		# record change in motion detection
		logger.debug('motion changed...')
		cur.execute("INSERT INTO Motion (unix_time, Date, Time, Status) values(?,?,?,?)",
							(int(time.mktime(today.timetuple())), today.date().strftime('%Y-%m-%d').lstrip('0'), str(d.time()),
															motionStatus))
		con.commit()
		motionDetected = motionStatus

print 'Logging sensor measurements to {0} every {1} seconds.'.format(SQL_DB_NAME, FREQUENCY_SECONDS)
logger.info('Office monitor start to %s' % SQL_DB_NAME)
print 'Press Ctrl-C to quit.'

# to use Raspberry Pi board pin numbers  
# GPIO.setmode(GPIO.BOARD) 
GPIO.setmode(GPIO.BCM) 
GPIO.setwarnings(False)
GPIO.cleanup()
# set up GPIO output channel  
GPIO.setup(GREEN_LED, GPIO.OUT, initial=GPIO.HIGH)  
GPIO.setup(RED_LED, GPIO.OUT, initial=GPIO.LOW)  
GPIO.setup(PIR_PIN, GPIO.IN)  # activate input
ledOn(GREEN_LED)
ledOff(RED_LED)

con = None
tempWarning = False
humidityWarning = False
tempWarningIssued = False
humidityWarningIssued = False
time.sleep(2)

try:
	while True:
		tickCounter += 1
		# Login if necessary.
		if con is None:
			con = open_db(SQL_DB_NAME)
		cur = con.cursor()   
			
		d = datetime.datetime.now()
		monitorMotion(config, con, d)

		# Attempt to get sensor reading.
		humidity, temp = Adafruit_DHT.read(DHT_TYPE, DHT_PIN)
	
		# Skip to the next reading if a valid measurement couldn't be taken.
		# This might happen if the CPU is under a lot of load and the sensor
		# can't be reliably read (timing is critical to read the sensor).
		if humidity is None or temp is None:
			time.sleep(2)
			continue
		tempF = 9.0 / 5.0 * temp + 32	
		logger.info('Temperature: {0:0.1f} C-{1:0.1f} f {2:0.1f} %'.format(temp, tempF, humidity))
		# if temp too  high or humidity too low turn on red led, else turn on green
		tempWarning = (tempF > TempHigh or tempF < TempLow)
		humidityWarning = (humidity < HumidityLow or humidity > HumidityHigh)
		if tempWarning or humidityWarning:
			if tempWarning and not tempWarningIssued:
				# turn on red
				ledOn(RED_LED)
				ledOff(GREEN_LED)
				tempWarningIssued = True
				try:
					if tempF > TempHigh:
						twarning = twitterApi.statuses.update("Jim office warning: temp too high - %s!" % tempF)
					else:
						twarning = twitterApi.statuses.update("Jim office warning: temp too low - %s!" % tempF)
				except:
					pass
			if humidityWarning and not humidityWarningIssued:
				# turn on red
				ledOn(RED_LED)
				ledOff(GREEN_LED)
				humidityWarningIssued = True
				try:
					if humidity > HumidityHigh:
						twarning = twitterApi.statuses.update("Jim office warning: humidity too high - %s!" % humidity)
					else:
						twarning = twitterApi.statuses.update("Jim office warning: humidity too low - %s!" % humidity)
				except:
					pass
			logger.warning('Temp or humidity out of range!')
		else:
			# turn on green
			if tempWarningIssued or humidityWarningIssued:
				logger.info('Temp/humidity is now normal.')
				ledOff(RED_LED)
				ledOn(GREEN_LED)
				try:
					tnormal = twitterApi.statuses.update("Jim office normal!")
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
			thisHour = d.time().hour
			if int(thisHour) != currentHour:
				# save averages for last hour in its cell
				# cell 16 for hour 0 temp, cell 17 for hour 0 humidity etc
				c1 = 'Temp'
				c2 = 'Hum'
				sqlcmd = "UPDATE Monitor SET TempHour%s=?, HumidityHour%s=? WHERE ID=?;" % (currentHour, currentHour)
				cur.execute(sqlcmd, ('{0:0.1f}'.format(lastRow[14]), '{0:0.1f}'.format(lastRow[16]), lastRowNum))
				
		# if date changed, insert new row
		# logger.debug('lastDate=%s curDate=%s' % (lastDate, d.date().strftime('%m/%d/%Y').lstrip('0')))
		if lastDate != d.date().strftime('%Y-%m-%d').lstrip('0'):
			# Append the data in the spreadsheet, including a timestamp
			try:
				cur.execute("INSERT INTO Monitor (unix_time, Date, Time, TempC, TempF, Humidity, TempFMin, TempFMax, HumidityMin, HumidityMax, CurrentHour, ReadingsInHour, SumTempForHour, AvgTempForHour, SumHumidityForHour, AvgHumidityForHour) values(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
							(int(time.mktime(d.timetuple())), d.date().strftime('%Y-%m-%d').lstrip('0'), str(d.time()),
															temp, tempF, humidity, tempF, tempF, humidity, humidity,
															0, 0, 0.0, 0.0, 0.0, 0.0))
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
				# Null out the worksheet so a login is performed at the top of the loop.
				logger.error('Insert error, opening again: %s' % traceback.format_exc())
				cur = None
				time.sleep(FREQUENCY_SECONDS)
				continue
		else:
			readingsInHour = int(lastRow[12]) + 1
			sumTempForHour = float(lastRow[13]) + tempF
			avgTempForHour = float(lastRow[13]) / readingsInHour
			sumHumForHour = float(lastRow[15]) + humidity
			avgHumForHour = float(lastRow[15]) / readingsInHour
				
		# print 'Summary data: %s %s %s %s %s %s' % (lastRow[9], lastRow[10], lastRow[11], lastRow[12], lastRow[13], lastRow[14])
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
			sqlcmd = "UPDATE Monitor SET unix_time=?, Time=?, TempC=?, TempF=?, Humidity=? WHERE ID=?;"
			cur.execute(sqlcmd,
					(int(time.mktime(d.timetuple())), str(d.time()), '{0:0.1f}'.format(temp), '{0:0.1f}'.format(tempF), '{0:0.1f}'.format(humidity), lastRowNum))
			# logger.debug('lastRow={0}'.format(lastRow))
			minTemp = min(tempF, float(lastRow[7]))
			maxTemp = max(tempF, float(lastRow[8]))
			minHum = min(humidity, float(lastRow[9]))
			maxHum = max(humidity, float(lastRow[10]))
			cur.execute("UPDATE Monitor SET TempFMin=?, TempFMax=?, HumidityMin=?, HumidityMax=? WHERE ID=?;",
					('{0:0.1f}'.format(minTemp), '{0:0.1f}'.format(maxTemp), '{0:0.1f}'.format(minHum), '{0:0.1f}'.format(maxHum), lastRowNum))
		except:
			logger.error('Update error, terminating: %s' % traceback.format_exc())
			break
		con.commit()
		# Wait 30 seconds before continuing
		logger.debug('Updated row {0}'.format(lastRowNum))
		time.sleep(FREQUENCY_SECONDS)
finally:
	if con:
		con.close()
	GPIO.cleanup() 
	logger.info('Cleaning up and exiting.') 
