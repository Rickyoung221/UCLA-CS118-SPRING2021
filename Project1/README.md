UID:000000000

# High Level Design:
The server set up a socket and monitor the socket. Whenever a client connection comes in, fork creates a new child process to respond to the client's request, and then parses the content sent from the client. If it is a GET request, then it needs to parse out the requested file, check the mimi structure information according to the file suffix. 
If the mimi is not recognized, replace it with the default one, and then try to open the file. 
If the file has spaces, parse out the location of the spaces. 
For the bonus part, if the file name contained with space, the space are recognized as '%20'.

Furthermore, the server checks if the file exists, if yes, opens it and reads inside data from the file. Then, build the response body with the response status.
If the request file does not exist, the server returns HTTP status code 404, which means the resource does not exist. Otherwise, it returns 200, which means OK. 
Message, if the file is successfully opened, then all file contents are sent in a loop, and finally the socket is closed.

# Problems Related:
1. Reading from files. It spent me a time to make this right. Finally, I use buffer to store and read the data. 
2. Programming Language. I am not truly familiar with using C/C++ language in backend development, it did cost me time to design.


The command to run:
	$ make
	$ ./webserver 8081 ./

URL to visit the file:
http://127.0.0.1:8081/filename
Or
localhost:8081/test.html

# Resources:
1. TA's slides
2. Internship Content
3. CS144 content (but using java)
