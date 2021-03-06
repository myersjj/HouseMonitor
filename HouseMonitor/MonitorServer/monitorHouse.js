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
require('string.prototype.startswith');
require('string.prototype.endswith');

var http = require('http');
var dblite = require('dblite');
var PythonShell = require('python-shell');

// Use node-static module to serve chart for client-side dynamic graph
var nodestatic = require('node-static');

// Setup static server for current directory
var staticServer = new nodestatic.Server(".");

var vm = require("vm");
var fs = require("fs");

// Setup database connection for logging
var db = dblite('/home/pi/HouseMonitor/house-monitor.db');

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
	// - Num records is an SQL filter from latest record back through time series,
	// - start_date is the first date in the time-series required,
	// - callback is the output function
	// unix_time is UTC, so convert to local: datetime(strftime('%s','now'), 'unixepoch', 'localtime')
	var current_temp = db.query('.headers ON')
			.query(
					"SELECT * FROM (SELECT * FROM Temperature WHERE locationId=? AND date(date) >= date(?) AND date(date) <= date(?, '+1 days')) ORDER BY date;",
					[ locationId, start_date, end_date ], function(err, rows) {
						if (err) {
							response.writeHead(500, {
								"Content-type" : "text/html"
							});
							response.end(err + "\n");
							console.log('Error serving querying database. '
									+ err);
							return;
						}
						//console.log(dumpObj(rows, 'Temp rows=', ' ', 5));
						data = {
								temperature_range : [ 65, 75],
								temperature_record : [ rows ]
							};
						callback(data);
					});
}

//Get humidity records from database
function selectHum(response, locationId, num_records, start_date, end_date, callback) {
	// - Num records is an SQL filter from latest record back through time series,
	// - start_date is the first date in the time-series required,
	// - callback is the output function
	// unix_time is UTC, so convert to local: datetime(strftime('%s','now'), 'unixepoch', 'localtime')
	var current_hum = db.query('.headers ON')
			.query(
					"SELECT * FROM (SELECT * FROM Humidity WHERE locationId=? AND date(date) >= date(?) AND date(date) <= date(?, '+1 days')) ORDER BY date;",
					[ locationId, start_date, end_date ], function(err, rows) {
						if (err) {
							response.writeHead(500, {
								"Content-type" : "text/html"
							});
							response.end(err + "\n");
							console.log('Error serving querying database. '
									+ err);
							return;
						}
						//console.log(dumpObj(rows, 'Hum rows=', ' ', 5));
						data = {
								humidity_range : [35, 60],
								humidity_record : [ rows ]
							}
						callback(data);
					});
}

//Get motion records from database
function selectMotion(response, locationId, num_records, start_date, end_date, callback) {
	// - Num records is an SQL filter from latest record back through time series,
	// - start_date is the first date in the time-series required,
	// - callback is the output function
	var current_motion = db.query('.headers ON')
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
						//console.log(dumpObj(rows, 'Motion rows=', ' ', 5));
						data = {
							motion_record : [ rows ]
						}
						callback(data);
					});
};

//Get motion records from database
function selectBattery(response, locationId, num_records, start_date, end_date, callback) {
	// - Num records is an SQL filter from latest record back through time series,
	// - start_date is the first date in the time-series required,
	// - callback is the output function
	var current_motion = db.query('.headers ON')
			.query(
					"SELECT * FROM (SELECT * FROM Battery WHERE locationId=? AND date(date) >= date(?) AND date(date) <= date(?, '+1 days') ORDER BY unix_time DESC LIMIT ?) ORDER BY unix_time;",
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
						//console.log(dumpObj(rows, 'Battery rows=', ' ', 5));
						data = {
								battery_range : [50, 100],
								battery_record : [ rows ]
						}
						console.log(dumpObj(data, 'Battery data=', ' ', 5));
						callback(data);
					});
};

//Get location records from database
function selectLocations(response, callback) {
	// - callback is the output function
	var locations = db.query('.headers ON')
			.query(
					"SELECT l.id, locationname, l.status, (select battery.status FROM battery,location where battery.locationid=l.id order by battery.id desc LIMIT 1) as bs FROM location l ORDER BY l.id;",
					[ ], {
					    Id: Number,
					    LocationName: String,
					    status: String,
					    bs: Number
					  }, 
					  function(err, rows) {
						if (err) {
							response.writeHead(500, {
								"Content-type" : "text/html"
							});
							response.end(err + "\n");
							console.log('Error serving querying database. '
									+ err);
							return;
						}
						console.log(rows.length);
						if (rows.length > 0 ) {
							data = {
									location_record : [ rows ]
							}
						} else data = {};
						callback(data);
					});
	console.log('locations=' + locations);
};

function addLocation(json) {
	try {
		console.log('add new location=' + json.newName);
		db.query("SELECT max(Id) FROM Location", [], {id: Number}, function(err, rows) {
			if (err) {
				response.writeHead(500, {
					"Content-type" : "text/html"
				});
				response.end(err + "\n");
				console.log('Error serving querying database. '
						+ err);
				return;
			}
			console.log(rows.length);
			var newId = 1;
			if (rows.length > 0) newId = rows[0].id + 1;
			var status = 'inactive';
			db.query('BEGIN');
			db.query(
					  'INSERT INTO Location VALUES (:id, :locationname, :status)',
					  {
					    id: newId,
					    locationname: json.newName,
					    status: status
					  }
					);
			db.query('COMMIT');
			console.log("inserted new locationid " + newId + ":" + json.newName);
		});
	} catch (e) {
		console.log('do addLocation failed:' + e);
	}
}

function updateLocation(json) {
	try {
		console.log('update location=' + json.id);
		db.query('BEGIN');
		db.query('UPDATE Location SET locationname=:locationname WHERE id=:id',
					  {
					    id: json.id,
					    locationname: json.name
					  }
					);
		db.query('COMMIT');
		console.log("updated locationid " + json.id + ":" + json.name);
	} catch (e) {
		console.log('do updateLocation failed:' + e);
	}
}

function handlePost(request, response, pathfile, json) {
	if (pathfile == '/addLocation.json') {
		console.log('handle addLocation');
		addLocation(json);
		response.writeHead(200, {
				"Content-type" : "application/json"
		});
		response.end();
		return;
	}
	if (pathfile == '/updateLocation.json') {
		console.log('handle updateLocation');
		updateLocation(json);
		response.writeHead(200, {
				"Content-type" : "application/json"
		});
		response.end();
		return;
	}
	console.log('handle python script POST, args=%r' % json);
	var options = {
			  mode: 'text',
			  pythonPath: '/usr/bin/python',
			  pythonOptions: ['-u'],
			  scriptPath: '.',
			  args: [json,]
			};
	PythonShell.run(pathfile, options, function (err, results) {
			  if (err) {
				  console.log(err);
				  return;
				  //throw err;
			  }
			  // results is an array consisting of messages collected during execution 
			  //console.log('results: %j', results);
			  console.log('results[0]=' + results[0]);
			  console.log('results[3]=' + results[3]);
			  //console.log('results[4]=' + results[4]);
			  response.writeHead(200, {
			  	"Content-type" : results[0]
			  });
			  //var json = [{jsonResponse: results[3]}];
			  //console.log('json=' + JSON.stringify(results[3]));
			  response.write(results[3]);
			  response.end();
			  return;
			});
	return;
}

function returnEnvData(response, tempData, humData, motionData) {
	data = { monitor_data : {
		temperature_record : tempData.temperature_record,
		temperature_range : tempData.temperature_range,
		humidity_record : humData.humidity_record,
		humidity_range : humData.humidity_range,
		motion_record : motionData.motion_record,
		battery_record: batteryData.battery_record,
		battery_range : batteryData.battery_range
		}
	};
	response.end(JSON.stringify(data), "ascii");
}

// Setup node http server
var server = http.createServer(
// Our main server function
function(request, response) {
	// Grab the URL requested by the client and parse any query options
	var url = require('url').parse(request.url, true);
	var pathfile = url.pathname;
	var query = url.query;
	console.log(request.method + ':path=' + pathfile + '\n\t' + dumpObj(query, 'query', ' ', 5));
	
	if ( request.method === 'POST' ) {
        // the body of the POST is JSON payload.
		if (pathfile.startsWith('/cgi-bin')) {
			var postdata = '';
			request.addListener('data', function(chunk) { postdata += chunk; });
			request.addListener('end', function() {
				console.log('post input=' + postdata);
				handlePost(request, response, pathfile, postdata);
			});
		} else {
			var data = '';
			request.addListener('data', function(chunk) { data += chunk; });
			request.addListener('end', function() {
				json = JSON.parse(data);
				console.log(dumpObj(json, 'json', ' ', 5));
				handlePost(request, response, pathfile, json);
			});
		}
        return;
    }

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
		// call selectEnv function to get data from database
		var tempData, humData, motionData;
		selectTemp(response, locationId, num_obs, start_date, end_date, function(data) {
			if (callbacks == 0) {
				response.writeHead(200, {
					"Content-type" : "application/json"
				});
			}
			callbacks++;
			tempData = data;
			if (callbacks == 4) {
				returnEnvData(response, tempData, humData, motionData, batteryData);
			}
		});
		selectHum(response, locationId, num_obs, start_date, end_date, function(data) {
			if (callbacks == 0) {
				response.writeHead(200, {
					"Content-type" : "application/json"
				});
			}
			callbacks++;
			humData = data;
			if (callbacks == 4) {
				returnEnvData(response, tempData, humData, motionData, batteryData);
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
			if (callbacks == 4) {
				returnEnvData(response, tempData, humData, motionData, batteryData);
			}
		});
		// call selectBattery function to get data from database
		selectBattery(response, locationId, num_obs, start_date, end_date, function(data) {
			if (callbacks == 0) {
				response.writeHead(200, {
					"Content-type" : "application/json"
				});
			}
			callbacks++;
			batteryData = data;
			if (callbacks == 4) {
				returnEnvData(response, tempData, humData, motionData, batteryData);
			}
		});
		return;
	}
	
	if (pathfile == '/getLocations.json') {
		// call selectLocations function to get data from database
		selectLocations(response, function(data) {
			if (callbacks == 0) {
				response.writeHead(200, {
					"Content-type" : "application/json"
				});
			}
			callbacks++;
			data = { monitor_data : data
			};
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
