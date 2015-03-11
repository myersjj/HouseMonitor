#!/usr/bin/python

# Google Spreadsheet for monitor data by Jim Myers (myersjj@gmail.com)
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
import time
import datetime
import logging

import Adafruit_DHT
import gspread
import gdata.docs.client

# Type of sensor, can be Adafruit_DHT.DHT11, Adafruit_DHT.DHT22, or Adafruit_DHT.AM2302.
DHT_TYPE = Adafruit_DHT.AM2302

# Example of sensor connected to Raspberry Pi pin 23
DHT_PIN = 23
# Example of sensor connected to Beaglebone Black pin P8_11
# DHT_PIN  = 'P8_11'

# Google Docs account email, password, and spreadsheet name.
GDOCS_EMAIL = 'myersjj@gmail.com'
GDOCS_PASSWORD = 'iomxdoaymzeynnqc'  # my app specific password
GDOCS_SPREADSHEET_NAME = 'office-monitor'

# How long to wait (in seconds) between measurements.
FREQUENCY_SECONDS = 30


def login_open_sheet(email, password, spreadsheet):
	"""Connect to Google Docs spreadsheet and return the first worksheet."""
	try:
		gc = gspread.login(email, password)
	except:
		print 'Unable to login.  Check email, password.'
		sys.exit(1)
	try:
		worksheet = gc.open(spreadsheet).sheet1
		return worksheet
	except:
		print 'Unable to locate spreadsheet.'
		docs_client = gdata.docs.client.DocsClient()
		docs_client.ClientLogin(GDOCS_EMAIL, GDOCS_PASSWORD, 'any')
		document = gdata.docs.data.Resource(type='spreadsheet', title=GDOCS_SPREADSHEET_NAME)
		resource = docs_client.CreateResource(document)
		# full_id = resource.resource_id.text # returned by gdata
		# gs_id = full_id[len('spreadsheet:'):]
		worksheet = gc.open(spreadsheet).sheet1
		worksheet.resize(1, 64)
		lastRowNum = worksheet.row_count
		assert lastRowNum == 1
		# initialize the new spreadsheet
		titleRow = ('Date', 'Time', 'Temp (C)', 'Temp (F)', 'Humidity (%)', 'Temp (F) Min', 'Temp (F) Max',
				'Humidity Min', 'Humidity Max', 'Current Hour', 'Readings in Hour', 'Sum Temp for Hour', 'Avg Temp for Hour',
				'Sum Humidity for Hour', 'Avg Humidity for Hour')
		for i in range(0, len(titleRow)):
			worksheet.update_cell(1, i + 1, titleRow[i])
		# add 48 columns for temp/humidity per hour values
		column = 16
		for hour in range(0, 24):
			worksheet.update_cell(1, column, 'Hour %s: Temp' % hour)
			worksheet.update_cell(1, column + 1, 'Hour %s: Humidity' % hour)
			column += 2
		# sys.exit(1)
		return worksheet

# create logger with 'spam_application'
logger = logging.getLogger('monitor')
logger.setLevel(logging.DEBUG)
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

print 'Logging sensor measurements to {0} every {1} seconds.'.format(GDOCS_SPREADSHEET_NAME, FREQUENCY_SECONDS)
logger.info('Office monitor start')
print 'Press Ctrl-C to quit.'

worksheet = None
while True:
	# Login if necessary.
	if worksheet is None:
		worksheet = login_open_sheet(GDOCS_EMAIL, GDOCS_PASSWORD, GDOCS_SPREADSHEET_NAME)
		
	# Attempt to get sensor reading.
	humidity, temp = Adafruit_DHT.read(DHT_TYPE, DHT_PIN)
	d = datetime.datetime.now()

	# Skip to the next reading if a valid measurement couldn't be taken.
	# This might happen if the CPU is under a lot of load and the sensor
	# can't be reliably read (timing is critical to read the sensor).
	if humidity is None or temp is None:
		time.sleep(2)
		continue
	tempF = 9.0 / 5.0 * temp + 32	
	logger.info('Temperature: {0:0.1f} C-{1:0.1f} f {2:0.1f} %'.format(temp, tempF, humidity))

	# get last row
	lastRowNum = worksheet.row_count
	# print 'lastRowNum=%s' % lastRowNum
	lastRow = worksheet.row_values(lastRowNum)
	# print 'lastRow=%s' % lastRow
	lastDate = lastRow[0]
	# print 'lastDate=%s today=%s' % (lastDate, d.date().strftime('%m/%d/%Y').lstrip('0'))
	if lastDate != d.date().strftime('%m/%d/%Y').lstrip('0'):
		# Append the data in the spreadsheet, including a timestamp
		try:
			worksheet.append_row((d.date().strftime('%m/%d/%Y').lstrip('0'), d.time(), temp, tempF, humidity, tempF, tempF, humidity, humidity,
							0, 0, 0.0, 0.0, 0.0, 0.0))
			lastRowNum = lastRowNum + 1
			lastRow = worksheet.row_values(lastRowNum)
			logger.debug('Wrote a new row to {0}'.format(GDOCS_SPREADSHEET_NAME))
		except:
			# Error appending data, most likely because credentials are stale.
			# Null out the worksheet so a login is performed at the top of the loop.
			logger.error('Append error, logging in again')
			worksheet = None
			time.sleep(FREQUENCY_SECONDS)
			continue
	# update current row with latest data
	worksheet.update_cell(lastRowNum, 2, d.time())
	worksheet.update_cell(lastRowNum, 3, '{0:0.1f}'.format(temp))
	worksheet.update_cell(lastRowNum, 4, '{0:0.1f}'.format(tempF))
	worksheet.update_cell(lastRowNum, 5, '{0:0.1f}'.format(humidity))
	minTemp = min(tempF, float(lastRow[5]))
	maxTemp = max(tempF, float(lastRow[6]))
	minHum = min(humidity, float(lastRow[7]))
	maxHum = max(humidity, float(lastRow[8]))
	worksheet.update_cell(lastRowNum, 6, '{0:0.1f}'.format(minTemp))
	worksheet.update_cell(lastRowNum, 7, '{0:0.1f}'.format(maxTemp))
	worksheet.update_cell(lastRowNum, 8, '{0:0.1f}'.format(minHum))
	worksheet.update_cell(lastRowNum, 9, '{0:0.1f}'.format(maxHum))
	# update rolling summary data
	currentHour = int(lastRow[9])
	thisHour = d.time().hour
	if int(thisHour) != currentHour:
		# save averages for last hour in its cell
		# cell 16 for hour 0 temp, cell 17 for hour 0 humidity etc
		worksheet.update_cell(lastRowNum, 16 + 2 * currentHour, lastRow[12])
		worksheet.update_cell(lastRowNum, 17 + 2 * currentHour, lastRow[14])
		# print 'reset summary data thisHour=%s currentHour=%s' % (thisHour, currentHour)
		lastRow[9] = thisHour
		lastRow[10] = 0
		lastRow[11] = 0.0
		lastRow[12] = 0.0
		lastRow[13] = 0.0
		lastRow[14] = 0.0
	lastRow[10] = int(lastRow[10]) + 1
	lastRow[11] = float(lastRow[11]) + tempF
	lastRow[12] = float(lastRow[11]) / int(lastRow[10])
	lastRow[13] = float(lastRow[13]) + humidity
	lastRow[14] = float(lastRow[13]) / int(lastRow[10])
	# print 'Summary data: %s %s %s %s %s %s' % (lastRow[9], lastRow[10], lastRow[11], lastRow[12], lastRow[13], lastRow[14])
	worksheet.update_cell(lastRowNum, 10, thisHour)
	worksheet.update_cell(lastRowNum, 11, lastRow[10])
	worksheet.update_cell(lastRowNum, 12, '{0:0.1f}'.format(lastRow[11]))
	worksheet.update_cell(lastRowNum, 13, '{0:0.1f}'.format(lastRow[12]))
	worksheet.update_cell(lastRowNum, 14, '{0:0.1f}'.format(lastRow[13]))
	worksheet.update_cell(lastRowNum, 15, '{0:0.1f}'.format(lastRow[14]))
	# Wait 30 seconds before continuing
	logger.debug('Updated row to {0}'.format(GDOCS_SPREADSHEET_NAME))
	time.sleep(FREQUENCY_SECONDS)
