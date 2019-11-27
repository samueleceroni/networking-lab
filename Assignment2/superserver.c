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
#define MAX_LINE_SIZE MAX_NAME_SIZE + PORT_NUMBER_SIZE + PROTOCOL_TYPE_SIZE + SERVICE_MODE_SIZE + 20

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
#define EXIT_DUP_ERROR 19
#define EXIT_EXECLE_ERROR 20
#define EXIT_WAIT_ERROR 21
#define EXIT_SUPERSERVER_CONFIG_FILE_ERROR 22
// Constants
#define PROTOCOL_UDP "udp"
#define PROTOCOL_TCP "tcp"
#define MODE_WAIT "wait"
#define MODE_NOWAIT "nowait"
#define PORT_MAX 65535
#define MAX_TCP_PENDING_CONNECTIONS 8
#define SUPERSERVER_CONF_FILE_NAME "superserver.conf"

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
	}
	exit(error);
}

// Checks if the given null-terminated string contains only whitespaces
bool is_empty(char *s) {
	for(; *s != '\0'; s++) {
		if (!isspace(*s)) {
			return false;
		}
	}
	return true;
}

// Counts non-empty lines in a stream
size_t count_lines(FILE *stream) {
	size_t lines = 0;
	bool prevNewline = true;
	char c;
	while((c = fgetc(stream)) != EOF) {
		if (c != '\n' && !isspace(c) && prevNewline) {
			lines++;
			prevNewline = false;
		} else if (c == '\n') {
			prevNewline = true;
		}
	}
	return lines;
}


ServiceDataVector read_configuration(FILE *stream) {
	// Initialize the vector
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
		do {
			char* result = fgets(line, MAX_LINE_SIZE, stream);
			if (result == NULL && !feof(stream)) {
				die(EXIT_READ_ERROR);
			}
		} while(is_empty(line));

		ServiceData *current = &config.services[index];
		current->pid = 0;

		int count = sscanf(line, formatString,
			current->path, current->protocol, current->port, current->mode);
		if (count != 4 ||
			(strcmp(PROTOCOL_UDP, current->protocol) != 0 && strcmp(PROTOCOL_TCP, current->protocol) != 0) ||
			(strcmp(MODE_WAIT, current->mode) != 0 && strcmp(MODE_NOWAIT, current->mode) != 0) ||
			(atoi(current->port) <= 0 || atoi(current->port) > PORT_MAX)) {
			die(EXIT_CONF_ERROR);
		}
		const char *lastSlash = strrchr(current->path, '/');
		strcpy(current->name, lastSlash == NULL ? current->path : lastSlash+1);
	}
	return config;
}

bool isServiceTcp(ServiceData* config) {
	return strcmp(config->protocol, PROTOCOL_TCP) == 0;
}

bool isServiceWait(ServiceData* config) {
	return strcmp(config->mode, MODE_WAIT) == 0;
}

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

void try_dup(int socketFD) {
	if (dup(socketFD) < 0)
		die(EXIT_DUP_ERROR);
}

pid_t try_wait(int* status) {
	pid_t pid = wait(status);
	if (pid < 0)
		die(EXIT_WAIT_ERROR);
	return pid;
}

void try_select(int highestFd, fd_set* readSet){
	if (select(highestFd, readSet, NULL, NULL, NULL) < 0 && errno != EINTR) {
		die(EXIT_SELECT_ERROR);
	}
}

struct sockaddr_in get_initialized_server_addr(ServiceData* config){
	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(atoi(config->port)); // Convert to network byte order
	serverAddr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
	return serverAddr;
}

void initialize_service(ServiceData *config) {
	bool isTcp = isServiceTcp(config);

	struct sockaddr_in serverAddr = get_initialized_server_addr(config);

	try_create_socket(config, isTcp);
	try_bind(config, serverAddr);
	if (isTcp) try_listen(config);
}

// Initialize all services, returns the highest file descriptor + 1 (useful for select).
int initialize_all_configs(ServiceDataVector *config) {
	int maxFd = -1;
	for (size_t i = 0; i < config->size; i++) {
		initialize_service(&config->services[i]);
		if (config->services[i].socketFD > maxFd) {
			maxFd = config->services[i].socketFD;
		}
	}
	return maxFd + 1;
}

// TODO remove, debug only
void print_config(ServiceDataVector config) {
	for (int i = 0; i < config.size; i++) {
		ServiceData *current = &config.services[i];
		printf("%s (%s) :%s, %s %s\n", current->path, current->name, current->port, current->mode, current->protocol);
	}
}

void free_services(ServiceDataVector *config) {
	free(config->services);
	config->size = 0;
}

// Never returns
void handle_service(int inputSocketFD, ServiceData *config, char * const envp[]) {
	try_close(0);
	try_dup(inputSocketFD);

	try_close(1);
	try_dup(inputSocketFD);

	try_close(2);
	try_dup(inputSocketFD);

	if (execle(config->path, config->name, envp) < 0)
		die(EXIT_EXECLE_ERROR);
}

ServiceDataVector read_server_configuration(){
	FILE *fp = fopen(SUPERSERVER_CONF_FILE_NAME, "r");
	if(fp == NULL)
		die(EXIT_SUPERSERVER_CONFIG_FILE_ERROR);
	ServiceDataVector config = read_configuration(fp);
	fclose(fp);
	return config;
}

void handle_positive_bit_select(ServiceData* config, char **env, fd_set* socketsSet){
	bool isTcp = isServiceTcp(config);
	int receiveSocketFD;
	if (isTcp) {
		receiveSocketFD = try_accept(config);
	} else {
		receiveSocketFD = config->socketFD;
	}

	pid_t pid = try_fork();

	if (pid == 0) {
		if (isTcp) {
			try_close(config->socketFD);
		}
		handle_service(receiveSocketFD, config, env); // Never returns
	}

	// In the father
	if (isTcp) {
		try_close(receiveSocketFD);
	}

	if (isServiceWait(config)) {
		FD_CLR(config->socketFD, socketsSet);
		config->pid = pid;
	}
}


//Function prototype devoted to handle the death of the son process
void handle_signal (int sig);

ServiceDataVector config;
fd_set socketsSet;

void initialize_socket_set(ServiceDataVector config, fd_set *socketsSet){
	FD_ZERO(socketsSet);
	for (size_t i = 0; i < config.size; i++) {
		printf("socket set %d\n", i);
		FD_SET(config.services[i].socketFD, socketsSet);
	}
}

void main_loop_one_cycle(int highestFd, char **env){
		fd_set readSet = socketsSet;
		try_select(highestFd, &readSet);

		for (size_t i = 0; i < config.size; i++) {
			if (FD_ISSET(config.services[i].socketFD, &readSet)) {
				handle_positive_bit_select(&config.services[i], env, &socketsSet);
			}
		}
}

int  main(int argc,char **argv,char **env){ // NOTE: env is the variable to be passed, as last argument, to execle system-call
	// Other variables declaration goes here
	
	// Configuration loading
	config = read_server_configuration();
	
	print_config(config); // TODO remove, debug only

	printf("initialize_all\n");
	// Server behavior implementation goes here
	int highestFd = initialize_all_configs(&config);
	printf("highestFd%d\n", highestFd);
	
	initialize_socket_set(config, &socketsSet);

	// Handle signals sent by son processes
	signal (SIGCHLD, handle_signal);

	while (true) {
		main_loop_one_cycle(highestFd, env);
	}

	free_services(&config);
	return 0;
}

// handle_signal implementation
void handle_signal (int sig){
	int childStatus;
	pid_t childPid;
	switch (sig) {
		case SIGCHLD:
			childPid = try_wait(&childStatus);
			if (!WIFEXITED(childStatus)) {
				fprintf(stderr, "A child with PID %d exited with code %d\n", childPid, WEXITSTATUS(childStatus));
			}
			for (size_t i = 0; i < config.size; i++) {
				if (config.services[i].pid == childPid) {
					FD_SET(config.services[i].socketFD, &socketsSet);
					config.services[i].pid = 0;
					break;
				}
			}
			break;
		default : printf ("Signal not known!\n"); break;
	}
}
