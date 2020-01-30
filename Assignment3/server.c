#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
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
#define EXIT_SEND_ERROR 26

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
		case EXIT_SEND_ERROR:
			perror("Cannot send to socket");
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

int try_accept(int socketFD){
	int acceptResult = accept(socketFD, NULL, NULL);
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

void try_send(int socketFD, char* buffer, size_t len) {
	int result = send(socketFD, buffer, len, 0);
	if (result < 0)
		die(EXIT_SEND_ERROR);
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

size_t receive_all_message(int socketFD, char *output) {
	size_t len = 0, last_read;
	do {
		last_read += try_recv(socketFD, output+len);
		len += last_read;
		printf("Received %s\n", output);
	} while(last_read != 0 && output[len-1] != '\n');
	return len;
}

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
	else if(strcmp(start, MEAS_THPUT) != 0)
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

	for (int i = 0; i < config.nProbes; i++) {
		int msgLen = receive_all_message(dataSocket, payloadBuffer);
		payloadBuffer[msgLen] = '\0';
		strcpy(originalMsg, payloadBuffer);
		int seqNumber;
		char *payload;
		if (!parse_measurement_msg(payloadBuffer, &seqNumber, &payload) || seqNumber != i || strlen(payload) != config.msgSize)
			return false;
		msleep(config.serverDelay);
		try_send(dataSocket, originalMsg, strlen(originalMsg));
	}
	free(payloadBuffer);
	free(originalMsg);
	return true;
}

// Returns false if there has been an error
bool handle_bye_phase(int dataSocket) {
	size_t len = receive_all_message(dataSocket, commonBuffer);
	return len == 2 && commonBuffer[0] == 'b' && commonBuffer[1] == '\n';
}

// Handles the communication with a client
void handle_communication(int dataSocket) {
	MeasurementConfig config;

	if (handle_hello_phase(dataSocket, &config)) {
		try_send(dataSocket, HELLO_OK_RESP, strlen(HELLO_OK_RESP));
	} else {
		try_send(dataSocket, HELLO_ERROR_RESP, strlen(HELLO_ERROR_RESP));
		return;
	}

	if (!handle_measurement_phase(dataSocket, config)) {
		try_send(dataSocket, MEASUREMENT_ERROR_RESP, strlen(MEASUREMENT_ERROR_RESP));
		return;
	}

	if (handle_bye_phase(dataSocket)) {
		try_send(dataSocket, BYE_OK_RESP, strlen(BYE_OK_RESP));
		return;
	} else {
		try_send(dataSocket, BYE_ERROR_RESP, strlen(BYE_ERROR_RESP));
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
		int dataSocket = try_accept(helloSocket);
		handle_communication(dataSocket);
		try_close(dataSocket);
	}
}
