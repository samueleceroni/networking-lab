#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include<unistd.h>
#include <stdbool.h>
#include <time.h>

#define MAX_BUF_SIZE 1024
#define PORT_MAX 65535
#define MEAS_THPUT "thput"
#define MEAS_RTT "rtt"
#define MEAS_THPUT_TYPE 0
#define MEAS_RTT_TYPE 1
#define MAX_INT_VALUE 1e8
#define MAX_INT_LENGTH 8
#define MAX_TCP_PENDING_CONNECTIONS 8

#define HELLO_OK_RESP "200 OK - Ready\n"
#define HELLO_ERROR_RESP "404 ERROR - Invalid Hello message\n"
#define MEASUREMENT_ERROR_RESP "404 ERROR - Invalid Measurement message\n"
#define BYE_OK_RESP "200 OK - Closing\n"
#define BYE_ERROR_RESP "404 ERROR - Invalid Bye message\n"

#define EXIT_SOCKET_CREATION_ERROR 12
#define EXIT_SOCKET_BIND_ERROR 13
#define EXIT_LISTEN_ERROR 14
#define EXIT_ACCEPT_ERROR 15
#define EXIT_CLOSE_ERROR 18
#define EXIT_INVALID_PORT 23
#define EXIT_RECV_ERROR 24
#define EXIT_MALLOC_ERROR 25

#define EXIT_SEND_ERROR_MSG "Cannot send to socket"

// General pourpose buffer
char commonBuffer[MAX_BUF_SIZE];

typedef struct {
	int measType;
	int nProbes;
	int msgSize;
	int serverDelay;
} MeasurementConfig;

// Terminates the program with a custom error code
void die(int error) {
	switch(error) {
		case EXIT_SOCKET_CREATION_ERROR:
			perror("The creation of the socket was unsuccesful");
			break;
		case EXIT_SOCKET_BIND_ERROR:
			perror("The bind of the port was unsuccesful");
			break;
		case EXIT_LISTEN_ERROR:
			perror("The listen returned an error");
			break;
		case EXIT_ACCEPT_ERROR:
			perror("Cannot accept connection");
			break;
		case EXIT_CLOSE_ERROR:
			perror("The close operation returned an error");
			break;
		case EXIT_INVALID_PORT:
			fprintf(stderr, "Usage: server PORT");
			break;
		case EXIT_RECV_ERROR:
			perror("Cannot read from socket");
			break;
		case EXIT_MALLOC_ERROR:
			perror("Cannot allocate memory");
			break;
	}
	exit(error);
}

/* TRY- functions: wrappers to system calls with error checking */

int try_create_tcp_socket(){
	int socketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(socketFD < 0)
		die(EXIT_SOCKET_CREATION_ERROR);
	return socketFD;
}

void try_bind(int socketFD, struct sockaddr_in serverAddr){
	if (bind(socketFD, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
		die(EXIT_SOCKET_BIND_ERROR);
}

void try_listen(int socketFD){
	if(listen(socketFD, MAX_TCP_PENDING_CONNECTIONS) < 0)
		die(EXIT_LISTEN_ERROR);
}

int try_accept(int socketFD, struct sockaddr_in* client_addr){
	socklen_t size = sizeof(*client_addr);
	int acceptResult = accept(socketFD, (struct sockaddr*)client_addr, &size);
	if(acceptResult < 0)
		die(EXIT_ACCEPT_ERROR);
	return acceptResult;
}

void try_close(int socketFD) {
	if (close(socketFD) < 0) {
		die(EXIT_CLOSE_ERROR);
	}
}

size_t try_recv(int socketFD, char* buffer) {
	int readCount = recv(socketFD, buffer, MAX_BUF_SIZE, 0);
	if (readCount < 0) {
		die(EXIT_RECV_ERROR);
	}
	return readCount;
}

bool try_send(int socketFD, char* buffer, size_t len) {
	int result = send(socketFD, buffer, len, MSG_NOSIGNAL);
	if (result < 0)
		perror(EXIT_SEND_ERROR_MSG);
	return result >= 0;
}

void* try_malloc(size_t size) {
	void* pointer = malloc(size);
	if (pointer == NULL)
		die(EXIT_MALLOC_ERROR);
	return pointer;
}

/* Utility functions */

struct sockaddr_in get_server_address(int port) {
	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port); // Convert to network byte order
	serverAddr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
	return serverAddr;
}

// Checks if the given string is a valid port number
bool is_valid_port(char* s) {
	if (strlen(s) > 5)
		return false;
	for(char *c = s; *c != 0; c++) {
		if (!isdigit(*c))
			return false;
	}
	return atoi(s) > 0 && atoi(s) <= PORT_MAX;
}

// Checks if a string ends with a newline
bool ends_with_newline(char *s, size_t len) {
	return len != 0 || s[len-1] != '\n';
}

// Sleeps for the given number of milliseconds
// Got from https://stackoverflow.com/a/1157217
int msleep(long msec) {
	struct timespec ts;
	int res;

	if (msec < 0) {
		errno = EINVAL;
		return -1;
	}

	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;

	do {
		res = nanosleep(&ts, &ts);
	} while (res && errno == EINTR);

	return res;
}

// Reads into *val an integer field at the beginning of the given string.
// The fields ends with the given character `delim`.
// Returns the first charater after the read int number, or NULL if the
// first field is not an integer or if the delimiter is not found.
char *read_int(char *s, char delim, int *val) {
	char *end = strchr(s, delim);
	if (end == NULL)
		return NULL;
	if (end - s > MAX_INT_LENGTH)
		return NULL;
	for(char *c = s; c < end; c++) {
		if(!isdigit(*c))
			return NULL;
	}
	*end = '\0';
	*val = atoi(s);
	*end = delim;
	return end;
}

// Reads from the socket until it finds a newline
size_t receive_all_message(int socketFD, char *output) {
	size_t len = 0, last_read;
	do {
		last_read = try_recv(socketFD, output+len);
		len += last_read;
	} while(last_read != 0 && output[len-1] != '\n');
	return len;
}

// Allocates the right amount of memory for a measurement message
char* allocate_measurement_message(int msgSize) {
	size_t totalSize = msgSize + MAX_INT_LENGTH + 10;
	return (char*)try_malloc(totalSize);
}

// Returns false if there has been an error
bool handle_hello_phase(int dataSocket, MeasurementConfig *conf) {
	char *msg = commonBuffer;
	size_t msgLen = receive_all_message(dataSocket, msg);

	if (!ends_with_newline(msg, msgLen))
		return false;
	// Check if it's hello message
	if (msg[0] != 'h' || msg[1] != ' ')
		return false;

	char *start, *end;

	// Check measurements type
	start = msg+2;
	end = strchr(start, ' ');
	if (end == NULL)
		return false;
	*end = '\0';
	if (strcmp(start, MEAS_RTT) == 0)
		conf->measType = MEAS_RTT_TYPE;
	else if(strcmp(start, MEAS_THPUT) == 0)
		conf->measType = MEAS_THPUT_TYPE;
	else
		return false;

	// Read number of probes
	start = end + 1;
	end = read_int(start, ' ', &conf->nProbes);
	if (end == NULL)
		return false;
	// Read msg size
	start = end + 1;
	end = read_int(start, ' ', &conf->msgSize);
	if (end == NULL)
		return false;
	// Read server delay
	start = end + 1;
	end = read_int(start, '\n', &conf->serverDelay);
	if (end == NULL)
		return false;

	return true;
}

// Returns false if there has been an error
bool parse_measurement_msg(char *msg, int *seqNumber, char **payload) {
	if (!ends_with_newline(msg, strlen(msg)))
		return false;
	// Check if it's measurement message
	if (msg[0] != 'm' || msg[1] != ' ')
		return false;

	char *start, *end;
	start = msg+2;
	end = read_int(start, ' ', seqNumber);
	if (end == NULL)
		return false;

	*payload = end + 1;
	start[strlen(start)-1] = 0; // Remove last newline

	return true;
}

// Returns false if there has been an error
bool handle_measurement_phase(int dataSocket, MeasurementConfig config) {
	char *payloadBuffer = allocate_measurement_message(config.msgSize);
	char *originalMsg = allocate_measurement_message(config.msgSize);

	bool isOk = true;
	for (int i = 1; isOk && i <= config.nProbes; i++) {
		int msgLen = receive_all_message(dataSocket, payloadBuffer);
		payloadBuffer[msgLen] = '\0';
		strcpy(originalMsg, payloadBuffer);
		int seqNumber;
		char *payload;
		// Check for any error
		if (!parse_measurement_msg(payloadBuffer, &seqNumber, &payload) || seqNumber != i || strlen(payload) != config.msgSize) {
			printf("Received wrong Measurement message\n");
			isOk = false;
			break;
		}
		printf("Received correct Measurement message with sequence number %d\n", seqNumber);
		msleep(config.serverDelay);
		isOk = try_send(dataSocket, originalMsg, strlen(originalMsg));
		printf("Echoed back Measurement message\n");
	}
	free(payloadBuffer);
	free(originalMsg);
	return isOk;
}

// Returns false if there has been an error
bool handle_bye_phase(int dataSocket) {
	size_t len = receive_all_message(dataSocket, commonBuffer);
	return len == 2 && commonBuffer[0] == 'b' && commonBuffer[1] == '\n';
}

// Handles the communication with a client
void handle_communication(int dataSocket) {
	MeasurementConfig config;

	// Handle Hello Phase errors and log messages
	if (handle_hello_phase(dataSocket, &config)) {
		const char *measType = config.measType == MEAS_RTT_TYPE ? "RTT" : "Throughput";
		printf("Received correct Hello message: measuring %s with %d probes of size %d, server delay of %dms\n",
			measType, config.nProbes, config.msgSize, config.serverDelay);
		if (!try_send(dataSocket, HELLO_OK_RESP, strlen(HELLO_OK_RESP)))
			return;
		printf("Sent OK response: %s", HELLO_OK_RESP);
	} else {
		printf("Received wrong Hello message\n");
		try_send(dataSocket, HELLO_ERROR_RESP, strlen(HELLO_ERROR_RESP));
		printf("Sent error response: %s", HELLO_ERROR_RESP);
		return;
	}

	// Handle Measurement Phase errors and log messages
	if (!handle_measurement_phase(dataSocket, config)) {
		try_send(dataSocket, MEASUREMENT_ERROR_RESP, strlen(MEASUREMENT_ERROR_RESP));
		printf("Sent error response: %s", MEASUREMENT_ERROR_RESP);
		return;
	}

	// Handle Bye Phase errors and log messages
	if (handle_bye_phase(dataSocket)) {
		printf("Received correct Bye message\n");
		try_send(dataSocket, BYE_OK_RESP, strlen(BYE_OK_RESP));
		printf("Sent OK response: %s", BYE_OK_RESP);
		return;
	} else {
		printf("Received wrong Bye message\n");
		try_send(dataSocket, BYE_ERROR_RESP, strlen(BYE_ERROR_RESP));
		printf("Sent error response: %s", BYE_ERROR_RESP);
		return;
	}
}

int main(int argc, char** argv) {
	// Read and check port parameter
	if (argc != 2 || !is_valid_port(argv[1])) {
		die(EXIT_INVALID_PORT);
	}
	int port = atoi(argv[1]);

	// Create the TCP socket to accept connections
	int helloSocket = try_create_tcp_socket();
	try_bind(helloSocket, get_server_address(port));
	try_listen(helloSocket);

	// Loop forever
	while(true) {
		// Accept a new connection
		struct sockaddr_in client_addr;
		int dataSocket = try_accept(helloSocket, &client_addr);

		// Print client info
		inet_ntop(AF_INET, &client_addr.sin_addr, commonBuffer, INET_ADDRSTRLEN);
		printf("Client connected: %s:%d\n", commonBuffer, client_addr.sin_port);

		// Handle the measurement
		handle_communication(dataSocket);

		// Close the connection (if the communication was either successful or failed)
		try_close(dataSocket);
		printf("Connection closed\n");
	}
}
