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


//TODO remove comment
//Service structure definition goes here
typedef struct {
	char protocol[4]; // 'tcp', 'udp'
	char mode[7]; // 'wait', 'nowait'
	char port[6]; // max is '65535'
	char path[255]; // service path
	char name[255]; // service name
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
