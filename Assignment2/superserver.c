#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<sys/wait.h>
#include<netinet/in.h>
#include<signal.h>
#include<errno.h>
#include<stdbool.h>
#include<ctype.h>
#include<unistd.h>

// Buffer sizes
#define PROTOCOL_TYPE_SIZE 4
#define SERVICE_MODE_SIZE 7
#define PORT_NUMBER_SIZE 6
#define MAX_NAME_SIZE 256
#define MAX_LINE_SIZE (MAX_NAME_SIZE + PORT_NUMBER_SIZE + PROTOCOL_TYPE_SIZE + SERVICE_MODE_SIZE + 20)

// Exit statuses
#define EXIT_READ_ERROR 10
#define EXIT_CONF_ERROR 11
#define EXIT_SOCKET_CREATION_ERROR 12
#define EXIT_SOCKET_BIND_ERROR 13
#define EXIT_LISTEN_ERROR 14
#define EXIT_ACCEPT_ERROR 15
#define EXIT_FORK_ERROR 16
#define EXIT_SELECT_ERROR 17
#define EXIT_CLOSE_ERROR 18
#define EXIT_WAIT_ERROR 19
#define EXIT_SUPERSERVER_CONFIG_FILE_ERROR 20
#define CHILD_EXIT_EXECLE_ERROR 21
#define CHILD_EXIT_CLOSE_ERROR 22
#define CHILD_EXIT_DUP_ERROR 23
// Constants
#define PROTOCOL_UDP "udp"
#define PROTOCOL_TCP "tcp"
#define MODE_WAIT "wait"
#define MODE_NOWAIT "nowait"
#define PORT_MAX 65535
#define MAX_TCP_PENDING_CONNECTIONS 8
#define SUPERSERVER_CONF_FILE_NAME "conf.txt"

typedef struct {
	char protocol[PROTOCOL_TYPE_SIZE]; // 'tcp', 'udp'
	char mode[SERVICE_MODE_SIZE]; // 'wait', 'nowait'
	char port[PORT_NUMBER_SIZE];
	char path[MAX_NAME_SIZE]; // service path
	char name[MAX_NAME_SIZE]; // service name
	int  socketFD;
	int  pid; // child process ID: only meaningful if type is 'wait'
} ServiceData;

typedef struct {
	size_t size;
	ServiceData *services;
} ServiceDataVector;

// Prints a custom error
void print_error(int error) {
	switch(error) {
		case EXIT_READ_ERROR:
			perror("An error occurred reading configuration file");
			break;
		case EXIT_CONF_ERROR:
			fprintf(stderr, "The configuration file is not formatted correctly");
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
		case EXIT_WAIT_ERROR:
			perror("The wait operation returned an error");
			break;
		case EXIT_SUPERSERVER_CONFIG_FILE_ERROR:
			fprintf(stderr, "The superserver failed to load the configuration file.\n");
			break;
		case CHILD_EXIT_EXECLE_ERROR:
			fprintf(stderr, "The execle operation returned an error\n");
			break;
		case CHILD_EXIT_CLOSE_ERROR:
			fprintf(stderr, "The close operation returned an error\n");
			break;
		case CHILD_EXIT_DUP_ERROR:
			fprintf(stderr, "The dup operation returned an error");
			break;
	}
}

// Terminates the program with a custom error code, printing a relevant error message
void die(int error) {
	print_error(error);
	exit(error);
}

// ========================= System calls wrappers =========================
// The following functions wraps system call and handles any error occurred.
// Functions starting with `child_` are meant to be used in a child process,
// the others in the parent process.

void try_create_socket(ServiceData *config, bool isTcp){
	config->socketFD = socket(AF_INET, isTcp ? SOCK_STREAM : SOCK_DGRAM, isTcp ? IPPROTO_TCP : IPPROTO_UDP);
	if(config->socketFD < 0)
		die(EXIT_SOCKET_CREATION_ERROR);
}

void try_bind(ServiceData *config, struct sockaddr_in serverAddr){
	if (bind(config->socketFD, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
		die(EXIT_SOCKET_BIND_ERROR);
}

void try_listen(ServiceData *config){
	if(listen(config->socketFD, MAX_TCP_PENDING_CONNECTIONS) < 0)
		die(EXIT_LISTEN_ERROR);
}

int try_accept(ServiceData *config){
	int acceptResult = accept(config->socketFD, NULL, NULL);
	if(acceptResult < 0)
		die(EXIT_ACCEPT_ERROR);
	return acceptResult;
}

void try_close(int socketFD) {
	if (close(socketFD) < 0) {
		die(EXIT_CLOSE_ERROR);
	}
}

pid_t try_fork() {
	pid_t result = fork();
	if (result < 0)
		die(EXIT_FORK_ERROR);
	return result;
}

pid_t try_wait(int* status) {
	pid_t pid = wait(status);
	if (pid < 0)
		die(EXIT_WAIT_ERROR);
	return pid;
}

// Returns true if there is some FD ready
bool try_select(int highestFd, fd_set* readSet){
	int result = select(highestFd, readSet, NULL, NULL, NULL);
	if (result < 0) {
		if (errno == EINTR) {
			return false;
		} else {
			die(EXIT_SELECT_ERROR);
		}
	}
	return true;
}

void child_try_close(int socketFD) {
	if (close(socketFD) < 0) {
		exit(CHILD_EXIT_CLOSE_ERROR);
	}
}

void child_try_dup(int socketFD) {
	if (dup(socketFD) < 0)
		exit(CHILD_EXIT_DUP_ERROR);
}

// ============================ Helper functions ===========================

// Checks if the given null-terminated string contains only whitespaces
bool is_empty(char *s) {
	for (; *s != '\0'; s++) {
		if (!isspace(*s)) {
			return false;
		}
	}
	return true;
}

// Checks if the given null-terminated string is a valid port number
bool is_valid_port(char* s) {
	if (strlen(s) > 5)
		return false;
	for (char *c = s; *c != 0; c++) {
		if (!isdigit(*c))
			return false;
	}
	return atoi(s) > 0 && atoi(s) <= PORT_MAX;
}

// Counts non-empty lines in a stream (a line is empty if there are no chars or only whites)
size_t count_lines(FILE *stream) {
	size_t lines = 0;
	bool prevNewline = true;
	char c;
	while ((c = fgetc(stream)) != EOF) {
		if (c != '\n' && !isspace(c) && prevNewline) {
			lines++;
			prevNewline = false;
		} else if (c == '\n') {
			prevNewline = true;
		}
	}
	return lines;
}

void print_config(ServiceDataVector config) {
	for (int i = 0; i < config.size; i++) {
		ServiceData *current = &config.services[i];
		printf("  %s (%s) :%s, %s %s\n", current->path, current->name, current->port, current->mode, current->protocol);
	}
}

ServiceDataVector read_configuration(FILE *stream) {
	// Initialize the conf vector counting the lines in conf file
	ServiceDataVector config;
	long fileBegin = ftell(stream);
	config.size = count_lines(stream);
	fseek(stream, fileBegin, SEEK_SET);
	config.services = (ServiceData*)malloc(config.size * sizeof(ServiceData));

	// Generate the format string to read parameters from a line
	char formatString[7*4];
	sprintf(formatString, "%%%ds %%%ds %%%ds %%%ds",
		MAX_NAME_SIZE-1, PROTOCOL_TYPE_SIZE-1, PORT_NUMBER_SIZE-1, SERVICE_MODE_SIZE-1);

	// Read and parse line by line
	char line[MAX_LINE_SIZE];
	for (size_t index = 0; index < config.size; index++) {
		do { // Find the next non-empty line
			char* result = fgets(line, MAX_LINE_SIZE, stream);
			if (result == NULL) { // Must be an error because we previously counted non-empty lines
				die(EXIT_READ_ERROR);
			}
		} while(is_empty(line));

		ServiceData *current = &config.services[index];
		current->pid = 0;

		// Extract data from the line and check validity
		int count = sscanf(line, formatString,
			current->path, current->protocol, current->port, current->mode);
		if (count != 4 ||
			(strcmp(PROTOCOL_UDP, current->protocol) != 0 && strcmp(PROTOCOL_TCP, current->protocol) != 0) ||
			(strcmp(MODE_WAIT, current->mode) != 0 && strcmp(MODE_NOWAIT, current->mode) != 0) ||
			!is_valid_port(current->port)) {
			die(EXIT_CONF_ERROR);
		}
		const char *lastSlash = strrchr(current->path, '/'); // Extract executable name from path
		strcpy(current->name, lastSlash == NULL ? current->path : lastSlash+1);
	}
	return config;
}

// Reads configuration from the right file
ServiceDataVector read_server_configuration(){
	FILE *fp = fopen(SUPERSERVER_CONF_FILE_NAME, "r");
	if(fp == NULL)
		die(EXIT_SUPERSERVER_CONFIG_FILE_ERROR);
	ServiceDataVector config = read_configuration(fp);
	printf("Configuration file read successfully:\n");
	print_config(config);
	fclose(fp);
	return config;
}

bool is_service_tcp(ServiceData* config) {
	return strcmp(config->protocol, PROTOCOL_TCP) == 0;
}

bool is_service_wait(ServiceData* config) {
	return strcmp(config->mode, MODE_WAIT) == 0;
}

// Gets the server address for binding the socket of the given service
struct sockaddr_in get_initialized_server_addr(ServiceData* config){
	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(atoi(config->port)); // Convert to network byte order
	serverAddr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
	return serverAddr;
}

// Initializes the socket for the given service
void initialize_service(ServiceData *config) {
	bool isTcp = is_service_tcp(config);

	struct sockaddr_in serverAddr = get_initialized_server_addr(config);

	try_create_socket(config, isTcp);
	try_bind(config, serverAddr);
	if (isTcp) try_listen(config);
}

// Initialize all services, returns the highest file descriptor + 1 (useful for select)
int initialize_all_services(ServiceDataVector *config) {
	int maxFd = -1;
	for (size_t i = 0; i < config->size; i++) {
		initialize_service(&config->services[i]);
		if (config->services[i].socketFD > maxFd) {
			maxFd = config->services[i].socketFD;
		}
	}
	return maxFd + 1;
}

// Frees a ServiceDataVector memory
void free_services(ServiceDataVector *config) {
	free(config->services);
	config->size = 0;
}

// Creates a child process for the servicel; never returns
void spawn_service(int inputSocketFD, ServiceData *config, char * const envp[]) {
	child_try_close(0);
	child_try_dup(inputSocketFD);

	child_try_close(1);
	child_try_dup(inputSocketFD);

	child_try_close(2);
	child_try_dup(inputSocketFD);

	if (execle(config->path, config->name, (char*)NULL, envp) < 0) {
		if (strcmp(config->protocol, PROTOCOL_UDP) == 0) {
			recv(inputSocketFD, NULL, 0, 0); // This should remove any pending data
		}
		exit(CHILD_EXIT_EXECLE_ERROR);
	}
}

// Handles a connection request for a service
void handle_service(ServiceData* config, char **env, fd_set* socketsSet){
	bool isTcp = is_service_tcp(config);
	int receiveSocketFD; // Socket to be used in the child
	if (isTcp) {
		receiveSocketFD = try_accept(config);
	} else {
		receiveSocketFD = config->socketFD;
	}
	printf("Handling service %s on %s port %s ('%s' mode).",
		config->path, config->protocol, config->port, config->mode);

	pid_t pid = try_fork();
	if (pid == 0) { // In the child
		if (isTcp) {
			try_close(config->socketFD);
		}
		spawn_service(receiveSocketFD, config, env); // Never returns
	}

	// From now on in the father
	printf(" Child PID is %d", pid);
	if (isTcp) {
		try_close(receiveSocketFD); // Close data TCP socket
	}
	if (is_service_wait(config)) {
		// Remove the socket from the select set
		FD_CLR(config->socketFD, socketsSet);
		// and save the child PID
		config->pid = pid;
		printf("; ignoring other socket activity.");
	}
	printf("\n");
}

void initialize_socket_set(ServiceDataVector config, fd_set *socketsSet){
	FD_ZERO(socketsSet);
	for (size_t i = 0; i < config.size; i++) {
		FD_SET(config.services[i].socketFD, socketsSet);
	}
}

//Function prototype devoted to handle the death of the son process
void handle_signal (int sig);

// These needs to be global because they are used in the signal handler
ServiceDataVector config;
fd_set socketsSet;

void main_loop(int highestFd, char **env){
	while(true) {
		fd_set readSet = socketsSet; // Copy the socketSet (select will modify it)
		bool somethingReady = try_select(highestFd, &readSet);

		if (somethingReady) { // Otherwise it has been interrupted by a signal
			for (size_t i = 0; i < config.size; i++) {
				if (FD_ISSET(config.services[i].socketFD, &readSet)) {
					handle_service(&config.services[i], env, &socketsSet);
				}
			}
		}
	}
}

int main(int argc, char **argv, char **env) {
	// Configuration loading
	config = read_server_configuration();

	int highestFd = initialize_all_services(&config);
	initialize_socket_set(config, &socketsSet);

	// Handle signals sent by son processes
	signal(SIGCHLD, handle_signal);

	main_loop(highestFd, env);

	free_services(&config);
	return 0;
}

// Signal handler function
void handle_signal(int sig) {
	int childStatus;
	pid_t childPid;
	switch (sig) {
		case SIGCHLD:
			childPid = try_wait(&childStatus);
			printf("PID %d exited\n", childPid);
			if (WEXITSTATUS(childStatus) != 0) {
				fprintf(stderr, "A child with PID %d exited with code %d\n", childPid, WEXITSTATUS(childStatus));
				print_error(WEXITSTATUS(childStatus));
			}
			// Add the service socket back to the select set
			for (size_t i = 0; i < config.size; i++) {
				if (config.services[i].pid == childPid) {
					FD_SET(config.services[i].socketFD, &socketsSet);
					config.services[i].pid = 0;
					printf("Service %s finished (PID %d); socket activity no longer ignored.\n",
						config.services[i].path, childPid);
					break;
				}
			}
			break;
		default:
			printf("Signal not known!\n");
			break;
	}
}
