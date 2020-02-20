#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/time.h>
#include <ctype.h>

#define MAX_BUF_SIZE 1024
#define PORT_MAX 65535
#define MEAS_THPUT "thput"
#define MEAS_RTT "rtt"
#define MEAS_THPUT_TYPE 0
#define MEAS_RTT_TYPE 1
#define MAX_INT_VALUE 1e8
#define MAX_INT_LENGTH 8

#define HELLO_OK_RESP "200 OK - Ready\n"
#define HELLO_ERROR_RESP "404 ERROR - Invalid Hello message\n"
#define MEASUREMENT_ERROR_RESP "404 ERROR - Invalid Measurement message\n"
#define BYE_OK_RESP "200 OK - Closing\n"
#define BYE_ERROR_RESP "404 ERROR - Invalid Bye message\n"

#define EXIT_SOCKET_CREATION_ERROR 12
#define EXIT_INVALID_PORT 23
#define EXIT_RECV_ERROR 24
#define EXIT_MALLOC_ERROR 25
#define EXIT_CONNECT_ERROR 27
#define EXIT_SEND_ERROR 28
#define EXIT_PARAMETERS_ERROR 29
#define EXIT_RESPONSE_ERROR 30

// General pourpose buffer
char commonBuffer[MAX_BUF_SIZE];
char *lastServerResponse;

// Used to measure time
struct timeval tm;

typedef struct {
	int measType;    // MEAS_THPUT_TYPE or MEAS_RTT_TYPE
	int nProbes;     // Number of probes to calculate the desired measurement
	int msgSize;     // The size of the measurement payload
	int serverDelay; // Used to simulate network propagation delay
} MeasurementConfig;

// Terminates the program with a custom error code
void die(int error) {
	switch(error) {
		case EXIT_SOCKET_CREATION_ERROR:
			perror("The creation of the socket was unsuccesful");
			break;
		case EXIT_INVALID_PORT:
			fprintf(stderr, "Usage: client ADDRESS PORT");
			break;
		case EXIT_CONNECT_ERROR:
			perror("Cannot connect to server");
			break;
		case EXIT_RECV_ERROR:
			perror("Cannot read from socket");
			break;
		case EXIT_MALLOC_ERROR:
			perror("Cannot allocate memory");
			break;
		case EXIT_SEND_ERROR:
			perror("Cannot send message");
			break;
		case EXIT_PARAMETERS_ERROR:
			fprintf(stderr, "Invalid measurement parameters");
			break;
		case EXIT_RESPONSE_ERROR:
			fprintf(stderr, "Error response from server: %s", lastServerResponse);
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

void try_connect(int socketFD, const char* address, int port) {
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(address);

	int res = connect(socketFD, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (res < 0)
		die(EXIT_CONNECT_ERROR);
}

void try_send(int socketFD, char *msg) {
	int len = strlen(msg);
	int res = send(socketFD, msg, len, MSG_NOSIGNAL);
	if (res < len)
		die(EXIT_SEND_ERROR);
}

size_t try_recv(int socketFD, char* buffer) {
	size_t readCount = recv(socketFD, buffer, MAX_BUF_SIZE, 0);
	if (readCount < 0) {
		die(EXIT_RECV_ERROR);
	}
	return readCount;
}

void* try_malloc(size_t size) {
	void* pointer = malloc(size);
	if (pointer == NULL)
		die(EXIT_MALLOC_ERROR);
	return pointer;
}


/* Utility functions */

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

// Checks validity of measurement configuration parameters
bool check_parameters(MeasurementConfig config) {
	if (config.nProbes <= 0 || config.nProbes > MAX_INT_VALUE)
		return false;
	if (config.msgSize <= 0 || config.msgSize > MAX_INT_VALUE)
		return false;
	if (config.serverDelay < 0 || config.serverDelay > MAX_INT_VALUE)
		return false;
	return true;
}

// Allocates the right amount of memory for a measurement message
char* allocate_measurement_message(int msgSize) {
	size_t totalSize = msgSize + MAX_INT_LENGTH + 10;
	return (char*)try_malloc(totalSize);
}

// Starts ther timer measurement
void start_timer_us() {
	gettimeofday(&tm, NULL);
}

// Stops timer measurement and returns the elapsed time
int stop_timer_us() {
	struct timeval tm2;
	gettimeofday(&tm2, NULL);
	return (tm2.tv_sec - tm.tv_sec) * 1e6 + (tm2.tv_usec - tm.tv_usec);
}

// Generates a cyclic payload like "abc...zabc..."
void generate_payload(int size, char* payload) {
	for(int i = 0; i < size; i++) {
		payload[i] = i % ('z'-'a'+1) + 'a';
	}
	payload[size] = '\0';
}

void print_measurement_result(MeasurementConfig config, double value) {
	const char *measType = config.measType == MEAS_RTT_TYPE ? "RTT" : "Throughput";
	const char *measUnit = config.measType == MEAS_RTT_TYPE ? "ms" : "kbps";
	printf("%s measured with %d probes with a payload of %d bytes: %.3f%s\n",
		measType, config.nProbes, config.msgSize, value, measUnit);
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

// Creates the hello message checking parameters validity
void create_hello_message(MeasurementConfig config, char *output) {
	const char *measurementType = config.measType == MEAS_RTT_TYPE ? MEAS_RTT : MEAS_THPUT;
	sprintf(output, "h %s %d %d %d\n", measurementType, config.nProbes, config.msgSize, config.serverDelay);
}

void create_measurement_message(int seqNum, char *payload, char *output) {
	sprintf(output, "m %d %s\n", seqNum, payload);
}

void create_bye_message(char *output) {
	strcpy(output, "b\n");
}

void handle_hello_phase(int socketFD, MeasurementConfig config) {
	create_hello_message(config, commonBuffer);
	try_send(socketFD, commonBuffer);
	printf("Sent Hello message\n");
	int readCount = receive_all_message(socketFD, commonBuffer);
	commonBuffer[readCount] = '\0';
	if (strcmp(commonBuffer, HELLO_OK_RESP) != 0) {
		lastServerResponse = commonBuffer;
		die(EXIT_RESPONSE_ERROR);
	}
	printf("Received OK Hello response\n");
}

// Returns the measurement result; -1 if an error occurred
double handle_measurement_phase(int socketFD, MeasurementConfig config) {
	char *payload = allocate_measurement_message(config.msgSize);
	char *outMessage = allocate_measurement_message(config.msgSize);
	char *inMessage = allocate_measurement_message(config.msgSize);
	generate_payload(config.msgSize, payload);

	int totalRtt = 0; // In microseconds

	for(int i = 0; i < config.nProbes; i++) {
		create_measurement_message(i, payload, outMessage);
		start_timer_us();
		try_send(socketFD, outMessage);
		printf("Sent probe with sequence number %d\n", i);
		int readCount = receive_all_message(socketFD, inMessage);
		int rtt = stop_timer_us();
		totalRtt += rtt;
		inMessage[readCount] = '\0';
		if (strcmp(inMessage, outMessage) != 0) {
			lastServerResponse = inMessage;
			die(EXIT_RESPONSE_ERROR);
		}
		printf("Received echoed probe %d, RTT was %.3fms\n", i, rtt/1000.0);
	}
	// Assuming the message size is always the same (only changes few bytes in the sequence number)
	int messageSize = strlen(inMessage);
	free(payload);
	free(outMessage);
	free(inMessage);
	double avgRtt = (double)totalRtt / config.nProbes / 1000;
	if (config.measType == MEAS_RTT_TYPE) {
		return avgRtt; // ms
	} else {
		return 8*messageSize / avgRtt; // bits / ms = kbps
	}
}

void handle_bye_phase(int socketFD) {
	create_bye_message(commonBuffer);
	try_send(socketFD, commonBuffer);
	int readCount = receive_all_message(socketFD, commonBuffer);
	commonBuffer[readCount] = '\0';
	if (strcmp(commonBuffer, BYE_OK_RESP) != 0) {
		lastServerResponse = commonBuffer;
		die(EXIT_RESPONSE_ERROR);
	}
}

// Handles a measuremente session with the server
void handle_session(int socketFD, MeasurementConfig config) {
	handle_hello_phase(socketFD, config);
	double result = handle_measurement_phase(socketFD, config);
	handle_bye_phase(socketFD);
	print_measurement_result(config, result);
}

// Carry out a complete measurement
void measure(const char* serverAddr, const int port, MeasurementConfig config) {
	// Connects to the server
	int serverSocket = try_create_tcp_socket();
	try_connect(serverSocket, serverAddr, port);
	handle_session(serverSocket, config);
}

// Reads configuration parameters
MeasurementConfig read_config() {
	MeasurementConfig config;
	char measType[20];
	fgets(commonBuffer, MAX_BUF_SIZE, stdin); // Only read one line
	int readCount = sscanf(commonBuffer, "%s %d %d %d", measType, &config.nProbes, &config.msgSize, &config.serverDelay);
	if (readCount < 3 || (strcmp(measType, MEAS_THPUT) != 0 && strcmp(measType, MEAS_RTT) != 0)) {
		die(EXIT_PARAMETERS_ERROR);
	}
	if (readCount == 3) { // Server delay is optional, default is 0
		config.serverDelay = 0;
	}
	if (!check_parameters(config)) {
		die(EXIT_PARAMETERS_ERROR);
	}
	if (strcmp(measType, MEAS_THPUT) == 0) {
		config.measType = MEAS_THPUT_TYPE;
	} else {
		config.measType = MEAS_RTT_TYPE;
	}
	return config;
}

int main(int argc, char **argv) {
	// Read and check port parameter
	if (argc != 3 || !is_valid_port(argv[2])) {
		die(EXIT_INVALID_PORT);
	}
	int port = atoi(argv[2]);

	MeasurementConfig config = read_config();
	measure(argv[1], port, config);
}
