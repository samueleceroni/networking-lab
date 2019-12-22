#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "myfunction.h"

#define MAX_BUF_SIZE 1024
#define SERVER_PORT 9876 // Server port #
#define BACK_LOG 2 // Maximum queued requests

int main(int argc, char *argv[]){
	struct sockaddr_in server_addr; // struct containing server address information
	struct sockaddr_in client_addr; // struct containing client address information
	int sfd; // Server socket file descriptor
	int newsfd; // Client communication socket -Accept result
	int br; // Bind result
	int lr; // Listen result
	int i;
	int stop = 0;
	ssize_t byteRecv; // Number of bytes received
	ssize_t byteSent; // Number of bytes to be sent

	socklen_t cli_size;
	char receivedData [MAX_BUF_SIZE]; // Data to be received
	char sendData [MAX_BUF_SIZE]; // Data to be sent

	sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sfd < 0){
		perror("socket"); // Print error message
		exit(EXIT_FAILURE);
	}

	// Initialize server address information
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT); // Convert to network byte order
	server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

	br = bind(sfd, (struct sockaddr *) &server_addr, sizeof(server_addr));

	if (br < 0){
		perror("bind"); // Print error message
		exit(EXIT_FAILURE);
	}
	cli_size = sizeof(client_addr);

	// Listen for incoming requests
	lr = listen(sfd, BACK_LOG);
	if (lr < 0) {
		perror("listen"); // Print error message
		exit(EXIT_FAILURE);
	}

	for (;;) {
		// Wait for incoming requests
		newsfd = accept(sfd, (struct sockaddr *) &client_addr, &cli_size);
		if (newsfd < 0){
			perror("accept"); // Print error message
			exit(EXIT_FAILURE);
		}

		while (1) {
			byteRecv = recv(newsfd, receivedData, sizeof(receivedData), 0);
			if (byteRecv < 0){
				perror("recv");
				exit(EXIT_FAILURE);
			}

			printf("Received data: ");
			printData(receivedData, byteRecv);
			if(strncmp(receivedData, "exit", byteRecv) == 0){
				printf("Command to stop server received\n");
				close(newsfd);
				break;
			}

			convertToUpperCase(receivedData, byteRecv);
			printf("Response to be sent back to client: ");
			printData(receivedData, byteRecv);

			byteSent = send(newsfd, receivedData, byteRecv, 0);
			if(byteSent != byteRecv){
				perror("send");
				exit(EXIT_FAILURE);
			}
		}
	} // End of for(;;)

	close(sfd);
	return 0;
}
