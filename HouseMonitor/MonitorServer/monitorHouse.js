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

// Use node-static module to server chart for client-side dynamic graph
var nodestatic = require('node-static');

// Setup static server for current directory
var staticServer = new nodestatic.Server(".");

// Setup database connection for logging
var db = dblite('/home/pi/house-monitor.db');

var MAX_DUMP_DEPTH = 10;

function dumpObj(obj, name, indent, depth) {
	if (depth > MAX_DUMP_DEPTH) {
		return indent + name + ": <Maximum Depth Reached>\n";
	}
	if (typeof obj == "object") {
		var child = null;
		var output = indent + name + "\n";
		indent += "\t";
		for ( var item in obj)
		{
			try {
				child = obj[item];
			} catch (e) {
				child = "<Unable to Evaluate>";
			}
			if (typeof child == "object") {
				output += dumpObj(child, item, indent, depth + 1);
			} else {
				output += indent + item + ": " + child + "\n";
			}
		}
		return output;
	} else {
		return obj;
	}
}

// Get temperature records from database
function selectTemp(response, locationId, num_records, start_date, end_date, callback) {
	// - Num records is an SQL filter from latest record back through time
	// series,
	// - start_date is the first date in the time-series required,
	// - callback is the output function
	// unix_time is UTC, so convert to local: datetime(strftime('%s','now'), 'unixepoch', 'localtime')
	var current_temp = db
			.query(
					"SELECT * FROM (SELECT * FROM Monitor WHERE locationId=? AND date(date) >= date(?) AND date(date) <= date(?, '+1 days') ORDER BY date DESC LIMIT ?) ORDER BY date;",
					[ locationId, start_date, end_date, num_records ], function(err, rows) {
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
								temperature_range : [ 65, 75],
								humidity_range : [35, 60],
								temperature_record : [ rows ]
							}
						console.log('Data=' + rows);
						callback(data);
					});
};

//Get motion records from database
function selectMotion(response, locationId, num_records, start_date, end_date, callback) {
	// - Num records is an SQL filter from latest record back through time
	// series,
	// - start_date is the first date in the time-series required,
	// - callback is the output function
	var current_motion = db
			.query(
					"SELECT * FROM (SELECT * FROM Motion WHERE locationId=? AND date(date) >= date(?) AND date(date) <= date(?, '+1 days') ORDER BY unix_time DESC LIMIT ?) ORDER BY unix_time;",
					[ locationId, start_date, end_date, num_records ], function(err, rows) {
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

// Setup node http server
var server = http.createServer(
// Our main server function
function(request, response) {
	// Grab the URL requested by the client and parse any query options
	var url = require('url').parse(request.url, true);
	var pathfile = url.pathname;
	var query = url.query;
	console.log('path=' + pathfile + dumpObj(query, '\nquery', 2, 5));

	// Test to see if it's a database query
	if (pathfile == '/monitor_house.json') {
		// Test to see if number of observations was specified as url query
		if (query.location_id) {
			var locationId = parseInt(query.location_id);
		} else {
			var locationId = 1;
		}
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
				+ request.connection.remoteAddress + ' for location ' + locationId + ' for ' + num_obs
				+ ' records from ' + start_date + ' to ' + end_date + '.');
		// call selectTemp function to get data from database
		var tempData, motionData;
		selectTemp(response, locationId, num_obs, start_date, end_date, function(data) {
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
						temperature_range : tempData.temperature_range,
						humidity_range : tempData.humidity_range,
						motion_record : motionData.motion_record
				}
				};
				response.end(JSON.stringify(data), "ascii");
			}
		});
		// call selectMotion function to get data from database
		selectMotion(response, locationId, num_obs, start_date, end_date, function(data) {
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
					temperature_range : tempData.temperature_range,
					humidity_range : tempData.humidity_range,
					motion_record : motionData.motion_record
				}
				};
				response.end(JSON.stringify(data), "ascii");
			}
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
