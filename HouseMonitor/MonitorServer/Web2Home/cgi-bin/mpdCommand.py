#!D:/Programme/Python27/python

import sys
import json
import cgi
import telnetlib

HOST = "10.79.109.197"
PORT = 6600

fs = cgi.FieldStorage()
command = fs.getvalue("cmd", "status")

client = telnetlib.Telnet(HOST, PORT)

client.write(command + "\n")
client.write("close\n")

data = client.read_all()
client.close()

jsonData = {
    'success': 'true',
    'cmd': command,
    'data': data
}

print "Content-type: application/json\n\n"
print json.dumps(jsonData)
