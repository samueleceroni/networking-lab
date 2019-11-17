#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<netinet/in.h>
#include<signal.h>
#include<errno.h>

// Buffer sizes
#define PROTOCOL_TYPE_SIZE 4
#define SERVICE_MODE_SIZE 7
#define PORT_NUMBER_SIZE 6
#define MAX_NAME_SIZE 256
#define MAX_LINE_SIZE MAX_NAME_SIZE + PORT_NUMBER_SIZE + PROTOCOL_TYPE_SIZE + SERVICE_MODE_SIZE + 20

// Exit statuses
#define EXIT_READ_ERROR 10
#define EXIT_CONF_ERROR 11

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
			fprintf(stderr, "An error occurred reading configuration file!");
			break;
		case EXIT_CONF_ERROR:
			fprintf(stderr, "The configuration file is not formatted correctly!");
			break;
	}
	exit(error);
}

// Counts non-empty lines in a stream
size_t count_lines(FILE *stream) { // TODO skip lines with only whitespaces
	size_t lines = 0;
	int prevNewline = 1;
	char c;
	while((c = fgetc(stream)) != EOF) {
		if (c != '\n' && prevNewline) {
			lines++;
			prevNewline = 0;
		} else if (c == '\n') {
			prevNewline = 1;
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
		} while(line[0] == '\n'); // TODO skip lines with only whitespaces

		ServiceData *current = &config.services[index];

		int count = sscanf(line, formatString,
			current->path, current->protocol, current->port, current->mode);
		if (count != 4 ||
			(strcmp("udp", current->protocol) != 0 && strcmp("tcp", current->protocol) != 0) || // TODO defines for these strings
			(strcmp("wait", current->mode) != 0 && strcmp("nowait", current->mode) != 0) ||
			(atoi(current->port) <= 0 || atoi(current->port) >= 65536)) {
			die(EXIT_CONF_ERROR);
		}
		const char *lastSlash = strrchr(current->path, '/');
		strcpy(current->name, lastSlash == NULL ? current->path : lastSlash+1);
	}
	return config;
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

//Function prototype devoted to handle the death of the son process
void handle_signal (int sig);

int  main(int argc,char **argv,char **env){ // NOTE: env is the variable to be passed, as last argument, to execle system-call
	// Other variables declaration goes here
	
	// Configuration loading
	FILE *fp = fopen("superserver.conf", "r"); // TODO more checks
	ServiceDataVector config = read_configuration(fp);
	fclose(fp);
	
	// Server behavior implementation goes here
	
	print_config(config); // TODO remove, debug only
	
	signal (SIGCHLD,handle_signal); /* Handle signals sent by son processes - call this function when it's ought to be */
	
	free_services(&config);
	return 0;
}

// handle_signal implementation
void handle_signal (int sig){
	// Call to wait system-call goes here
	
	
	switch (sig) {
		case SIGCHLD : 
			// Implementation of SIGCHLD handling goes here	
			
			
			break;
		default : printf ("Signal not known!\n");
			break;
	}
}
