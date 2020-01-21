#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_BUF_SIZE 1024
#define PORT_MAX 65535
#define MEAS_THPUT "thput"
#define MEAS_RTT "rtt"
#define MEAS_THPUT_TYPE 0
#define MEAS_RTT_TYPE 1
#define MAX_INT_VALUE 1e8
#define MAX_INT_LENGTH 8

#define EXIT_SOCKET_CREATION_ERROR 12
#define EXIT_INVALID_PORT 23
#define EXIT_RECV_ERROR 24

typedef struct {
	int measType;
	int nProbes;
	int msgSize;
	int serverDelay;
} MeasurementConfig;

// Terminates the program with a custom error code
void die(int error) {
	switch(error) {
		case EXIT_READ_ERROR:
			perror("An error occurred reading configuration file");
			break;
		case EXIT_CONF_ERROR:
			perror("The configuration file is not formatted correctly");
			break;
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
		case EXIT_FORK_ERROR:
			perror("Cannot create a forked process");
			break;
		case EXIT_SELECT_ERROR:
			perror("The select operation returned an error");
			break;
		case EXIT_CLOSE_ERROR:
			perror("The close operation returned an error");
			break;
		case EXIT_DUP_ERROR:
			perror("The dup operation returned an error");
			break;
		case EXIT_EXECLE_ERROR:
			perror("The execle operation returned an error");
			break;
		case EXIT_WAIT_ERROR:
			perror("The wait operation returned an error");
			break;
		case EXIT_SUPERSERVER_CONFIG_FILE_ERROR:
			fprintf(stderr, "The superserver failed to load the configuration file.");
			break;
		case EXIT_INVALID_PORT:
			fprintf(stderr, "Usage: server PORT");
			break;
		case EXIT_INVALID_PORT:
			perror("Cannot read from socket");
			break;
	}
	exit(error);
}

int is_valid_port(char* s) {
	if (strlen(s) > 5)
		return 0;
	for(char *c = s; *c != 0; c++) {
		if (!isdigit(*c))
			return 0;
	}
	return atoi(s) > 0 && atoi(s) <= PORT_MAX;
}

void try_create_tcp_socket(){
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

void get_server_address(int port) {
	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port); // Convert to network byte order
	serverAddr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
	return serverAddr;
}

void try_close(int socketFD) {
	if (close(socketFD) < 0) {
		die(EXIT_CLOSE_ERROR);
	}
}

void try_recv(int socketFD, char* buffer) {
	int readCount = recv(socketFD, buffer, MAX_BUF_SIZE, 0);
	if (readCount < 0) {
		die(EXIT_RECV_ERROR);
	}
}

int ends_with_newline(char *s) {
	int size = strlen(s);
	return size != 0 || s[size-1] != '\n';
}

// Returns the first charater after the read int
char *read_int(char *s, char delim, int *val) {
	char *end = strchr(s, delim);
	if (end == NULL)
		return NULL;
	*end = '\0';
	if (end - s > MAX_INT_LENGTH)
		return NULL;
	for(char *c = s; c < end; c++) {
		if(!isdigit(*c))
			return NULL;
	}
	*val = atoi(s);
	return end;
}

int handle_hello_message(char *msg, MeasurementConfig *conf) {
	if (!ends_with_newline(msg))
		return 0;
	// Check if it's hello message
	if (s[0] != 'h' || s[1] != ' ')
		return 0;

	char *start, *end;

	// Check measurements type
	start = msg+2;
	end = strchr(start, ' ');
	if (end == NULL)
		return 0;
	*end = '\0';
	if (strcmp(start, MEAS_RTT) == 0)
		conf->measType = MEAS_RTT_TYPE;
	else if(strcmp(start, MEAS_THPUT) != 0)
		conf->measType = MEAS_THPUT_TYPE;
	else
		return 0;

	// Read number of probes
	start = end + 1;
	end = read_int(start, ' ', &conf->nProbes);
	if (end == NULL)
		return 0;
	// Read msg size
	start = end + 1;
	end = read_int(start, ' ', &conf->msgSize);
	if (end == NULL)
		return 0;
	// Read server delay
	start = end + 1;
	end = read_int(start, '\n', &conf->serverDelay);
	if (end == NULL)
		return 0;
}

void handle_communication(int dataSocket) {
	char buffer[MAX_BUF_SIZE];
	try_recv(dataSocket, buffer);
	handle_hello_message(buffer);

}

int main(int argc, char** argv) {
	if (argc != 2 || !is_valid_port(argv[1])) {
		die(EXIT_INVALID_PORT);
	}
	int port = atoi(argv[1]);

	int helloSocket = try_create_tcp_socket();
	try_bind(helloSocket, get_server_address(port));
	try_listen(helloSocket);

	while(1) {
		int dataSocket = try_accept(helloSocket);
		handle_communication(dataSocket);
		try_close(dataSocket);
	}
}
