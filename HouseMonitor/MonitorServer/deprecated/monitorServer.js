// server.js - NodeJS server for the PiMonitorServer project.

/* 

 Parses data from DS18B20 temperature sensor and serves as a JSON object.
 Uses node-static module to serve a plot of current temperature (uses highcharts).

 Tom Holderness 03/01/2013
 Ref: www.cl.cam.ac.uk/freshers/raspberrypi/tutorials/temperature/
 */

// Load node modules
var fs = require('fs');
var sys = require('sys');
var log4js = require('log4js');
var logger = log4js.getLogger();

var http = require('http');
var dblite = require('dblite');
var Gpio = require('pi-gpio');
greenLED = 0;
redLED = 0;
blueLED = 0;
motion = 0;

// Use node-static module to server chart for client-side dynamic graph
var nodestatic = require('node-static');

// Setup static server for current directory
var staticServer = new nodestatic.Server(".");

// Setup database connection for logging
var db = dblite('/home/pi/office-monitor.db');

// Gpio.reset();
// Gpio.setMode(Gpio.MODE_BCM);
// Gpio.on('change', function(channel, value) {
// console.log('Channel ' + channel + ' value is now ' + value);
// logger.debug('Channel ' + channel + ' value is now ' + value);
// if (channel == 4) greenLED = value;
// if (channel == 17) redLED = value;
// if (channel == 18) motion = value;
// });
// use actual pin numbers, not GPIO numbers
try {
	Gpio.open(7, 'input');
} finally {
}
try {
	Gpio.open(11, 'input');
} finally {
}
try {
	Gpio.open(12, 'input pullup');
} finally {
}
// Gpio.setup(4, Gpio.DIR_IN);
// Gpio.setup(17, Gpio.DIR_IN);
// Gpio.setup(18, Gpio.DIR_IN);

var eeGreen = new process.EventEmitter();
eeGreen.on('stateChange', function(previousValue, value) {
	console.log('Green state changed from', previousValue, 'to', value);
});

setInterval(function() {
	Gpio.read(7, function(err, value) {
		if (err) {
			eeGreen.emit('error-green', err);
		} else {
			if (greenLED !== value) {
				var previousState = greenLED;
				greenLED = value;
				eeGreen.emit('stateChange', previousState, value);
			}
		}
	});
}, 1000); // check green state every 1000ms

var eeRed = new process.EventEmitter();
eeRed.on('stateChange', function(previousValue, value) {
	console.log('Red state changed from', previousValue, 'to', value);
});

setInterval(function() {
	Gpio.read(11, function(err, value) {
		if (err) {
			eeRed.emit('error-red', err);
		} else {
			if (redLED !== value) {
				var previousState = redLED;
				redLED = value;
				eeRed.emit('stateChange', previousState, value);
			}
		}
	});
}, 1000); // check red state every 1000ms

var eePIR = new process.EventEmitter();
eePIR.on('stateChange', function(previousValue, value) {
	console.log('PIR state changed from', previousValue, 'to', value);
});

setInterval(function() {
	Gpio.read(12, function(err, value) {
		if (err) {
			eePIR.emit('error-pir', err);
		} else {
			if (motion !== value) {
				var previousState = motion;
				motion = value;
				eePIR.emit('stateChange', previousState, value);
			}
		}
	});
}, 100); // check PIR state every 100ms

// Get temperature records from database
function selectTemp(response, num_records, start_date, end_date, callback) {
	// - Num records is an SQL filter from latest record back through time
	// series,
	// - start_date is the first date in the time-series required,
	// - callback is the output function
	// unix_time is UTC, so convert to local: datetime(strftime('%s','now'), 'unixepoch', 'localtime')
	var current_temp = db
			.query(
					"SELECT * FROM (SELECT * FROM Monitor WHERE date(date) >= date(?) AND date(date) <= date(?, '+1 days') ORDER BY date DESC LIMIT ?) ORDER BY date;",
					[ start_date, end_date, num_records ], function(err, rows) {
						if (err) {
							response.writeHead(500, {
								"Content-type" : "text/html"
							});
							response.end(err + "\n");
							console.log('Error serving querying database. '
									+ err);
							return;
						}
						data = {
							temperature_record : [ rows ]
						}
						console.log('Data=' + rows);
						callback(data);
					});
};

//Get motion records from database
function selectMotion(response, num_records, start_date, end_date, callback) {
	// - Num records is an SQL filter from latest record back through time
	// series,
	// - start_date is the first date in the time-series required,
	// - callback is the output function
	var current_motion = db
			.query(
					"SELECT * FROM (SELECT * FROM Motion WHERE date(date) >= date(?) AND date(date) <= date(?, '+1 days') ORDER BY unix_time DESC LIMIT ?) ORDER BY unix_time;",
					[ start_date, end_date, num_records ], function(err, rows) {
						if (err) {
							response.writeHead(500, {
								"Content-type" : "text/html"
							});
							response.end(err + "\n");
							console.log('Error serving querying database. '
									+ err);
							return;
						}
						data = {
							motion_record : [ rows ]
						}
						console.log('Data=' + rows);
						callback(data);
					});
};

// Get LED status
function queryLED(response, callback) {
	data = {
		led_record : [ greenLED, redLED, blueLED, motion ]
	};
	console.log('Data=' + data.led_record);
	callback(data);
}

// Setup node http server
var server = http.createServer(
// Our main server function
function(request, response) {
	// Grab the URL requested by the client and parse any query options
	var url = require('url').parse(request.url, true);
	var pathfile = url.pathname;
	var query = url.query;

	// Test to see if it's a database query
	if (pathfile == '/monitor_query.json') {
		// Test to see if number of observations was specified as url query
		if (query.num_obs) {
			var num_obs = parseInt(query.num_obs);
		} else {
			// If not specified default to 20. Note use -1 in query string to
			// get all.
			var num_obs = -1;
		}
		if (query.start_date) {
			var start_date = query.start_date;
		} else {
			var start_date = new Date().toISOString().slice(0, 10);
		}
		if (query.end_date) {
			var end_date = query.end_date;
		} else {
			var end_date = new Date().toISOString().slice(0, 10);
		}
		var callbacks = 0;
		// Send a message to console log
		console.log('Database query request from '
				+ request.connection.remoteAddress + ' for ' + num_obs
				+ ' records from ' + start_date + ' to ' + end_date + '.');
		// call selectTemp function to get data from database
		var tempData, motionData;
		selectTemp(response, num_obs, start_date, end_date, function(data) {
			if (callbacks == 0) {
				response.writeHead(200, {
					"Content-type" : "application/json"
				});
			}
			callbacks++;
			tempData = data;
			if (callbacks == 2) {
				data = { monitor_data : {
						temperature_record : tempData.temperature_record,
						motion_record : motionData.motion_record
				}
				};
				response.end(JSON.stringify(data), "ascii");
			}
		});
		// call selectMotion function to get data from database
		selectMotion(response, num_obs, start_date, end_date, function(data) {
			if (callbacks == 0) {
				response.writeHead(200, {
					"Content-type" : "application/json"
				});
			}
			callbacks++;
			motionData = data;
			if (callbacks == 2) {
				data = { monitor_data : {
					temperature_record : tempData.temperature_record,
					motion_record : motionData.motion_record
				}
				};
				response.end(JSON.stringify(data), "ascii");
			}
		});
		return;
	}
	if (pathfile == '/temperature_query.json') {
		// Test to see if number of observations was specified as url query
		if (query.num_obs) {
			var num_obs = parseInt(query.num_obs);
		} else {
			// If not specified default to 20. Note use -1 in query string to
			// get all.
			var num_obs = -1;
		}
		if (query.start_date) {
			var start_date = query.start_date;
		} else {
			var start_date = new Date().toISOString().slice(0, 10);
		}
		if (query.end_date) {
			var end_date = query.end_date;
		} else {
			var end_date = new Date().toISOString().slice(0, 10);
		}
		// Send a message to console log
		console.log('Database query request from '
				+ request.connection.remoteAddress + ' for ' + num_obs
				+ ' records from ' + start_date + ' to ' + end_date + '.');
		// call selectTemp function to get data from database
		selectTemp(response, num_obs, start_date, end_date, function(data) {
			response.writeHead(200, {
				"Content-type" : "application/json"
			});
			response.end(JSON.stringify(data), "ascii");
		});
		return;
	}

	// Test to see if it's a request for current temperature
	if (pathfile == '/temperature_now.json') {
		readTemp(function(data) {
			response.writeHead(200, {
				"Content-type" : "application/json"
			});
			response.end(JSON.stringify(data), "ascii");
		});
		return;
	}

	// Handler for favicon.ico requests
	if (pathfile == '/favicon.ico') {
		response.writeHead(200, {
			'Content-Type' : 'image/x-icon'
		});
		response.end();

		// Optionally log favicon requests.
		// console.log('favicon requested');
		return;
	}

	// handler for LED requests
	if (pathfile == '/office_query.json') {
		queryLED(response, function(data) {
			response.writeHead(200, {
				"Content-type" : "application/json"
			});
			response.end(JSON.stringify(data), "ascii");
		});
		return;
	}

	else {
		// Print requested file to terminal
		console.log('Request from ' + request.connection.remoteAddress
				+ ' for: ' + pathfile);

		// Serve file using node-static
		staticServer.serve(request, response,
				function(err, result) {
					if (err) {
						// Log the error
						sys.error("Error serving " + request.url + " - "
								+ err.message);

						// Respond to the client
						response.writeHead(err.status, err.headers);
						response.end('Error 404 - file not found');
						return;
					}
					return;
				})
	}
});

// Enable server
server.listen(8000);
// Log message
console.log('Server running at http://localhost:8000');

//this function is called when you want the server to die gracefully
//i.e. wait for existing connections
var gracefulShutdown = function() {
	console.log("Received kill signal, shutting down gracefully.");
	Gpio.close(7);
	Gpio.close(11);
	Gpio.close(12);
	server.close(function() {
		console.log("Closed out remaining connections.");
		process.exit()
	});

	// if after
	setTimeout(
			function() {
				console
						.error("Could not close connections in time, forcefully shutting down");
				process.exit()
			}, 10 * 1000);
}

//listen for TERM signal .e.g. kill 
process.on ('SIGTERM', gracefulShutdown);

//listen for INT signal e.g. Ctrl-C
process.on ('SIGINT', gracefulShutdown);   
