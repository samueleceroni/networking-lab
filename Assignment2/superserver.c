#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<netinet/in.h>
#include<signal.h>
#include<errno.h>

//Constants and global variable declaration goes here
 // 'tcp' or 'udp'
#define PROTOCOL_TYPE_SIZE 4
 // 'wait' or 'nowait'
#define SERVICE_MODE_SIZE 7
 // max is '65535'
#define PORT_NUMBER_SIZE 6
#define MAX_NAME_SIZE 255

//TODO remove comment
//Service structure definition goes here
typedef struct {
	char protocol[PROTOCOL_TYPE_SIZE]; // 'tcp', 'udp'
	char mode[SERVICE_MODE_SIZE]; // 'wait', 'nowait'
	char port[PORT_NUMBER_SIZE];
	char path[MAX_NAME_SIZE]; // service path
	char name[MAX_NAME_SIZE]; // service name
	int  socketFD;
	int  pid; // child process ID: only meaningful if type is 'wait'
} ServiceData;

//Function prototype devoted to handle the death of the son process
void handle_signal (int sig);

int  main(int argc,char **argv,char **env){ // NOTE: env is the variable to be passed, as last argument, to execle system-call
	// Other variables declaration goes here
	
	
	// Server behavior implementation goes here
	
	
	signal (SIGCHLD,handle_signal); /* Handle signals sent by son processes - call this function when it's ought to be */
	
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
