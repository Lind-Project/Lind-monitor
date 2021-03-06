/*
 * test_listen.c
 *
 *  Created on: Jun 19, 2014
 *      Author:  Ali Gholami
 *
 */

#include "testcases.h"

int main(int argc, char *argv[]) {

	pthread_t server, client;

	const char *message1 = "Server thread";
	const char *message2 = "Client thread";
	int ret1, ret2;

	ret1 = pthread_create(&server, NULL, test_listen, (void*) message1);

	if (ret1) {
		fprintf(stderr, "Error - pthread_create() return code: %d\n", ret1);
		return 1;
	}

	ret2 = pthread_create(&client, NULL, test_client, (void*) message2);

	if (ret2) {
		fprintf(stderr, "Error - pthread_create() return code: %d\n", ret2);
		return 1;
	}

	fprintf(stdout, "pthread_create() for server returns: %d\n", ret1);
	fprintf(stdout, "pthread_create() for client returns: %d\n", ret2);

	pthread_join(server, NULL);
	pthread_join(client, NULL);

	return 0;
}

void * test_listen() {

	/* taken from http://www.tenouk.com/Module41b.html */
	int len, rc, sd1, sd2;
	char buffer[MAXBUF];

	struct sockaddr_in addr;

	sd1 = socket(AF_INET, SOCK_STREAM, 0);

	if (sd1 < 0) {
		fprintf(stderr, "socket() error \n");
	}

	/* Bind the socket */
	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(LISTEN_SERVER_PORT);

	rc = bind(sd1, (struct sockaddr *) &addr, sizeof(addr));

	if (rc < 0) {
		fprintf(stderr, "bind() error \n");
		close(sd1);
		return NULL;
	}

	rc = listen(sd1, 5);

	if (rc < 0) {
		fprintf(stderr, "listen() error \n");
		close(sd1);
		return NULL;

	}

	sd2 = accept(sd1, NULL, NULL);

	if (sd2 < 0) {
		fprintf(stderr, "accept() error \n");
		close(sd1);
		return NULL;
	}

	rc = recv(sd2, buffer, sizeof(buffer), 0);

	if (rc <= 0) {
		fprintf(stderr, "recv() error \n");
		close(sd1);
		close(sd2);
		return NULL;
	}

	len = rc;

	rc = send(sd2, buffer, len, 0);

	if (rc <= 0) {
		fprintf(stderr, "send() error \n");
		close(sd1);
		close(sd2);
		return NULL;
	}

	if (close(sd1) != 0){
			fprintf(stderr, "close() error \n");
			return NULL;
	}

	if (close(sd2) != 0){
			fprintf(stderr, "close() error \n");
			return NULL;
	}

	return (void*) 1;
}

void * test_client()
{
	int len, rc;
	int sockfd;
	char send_buf[MAXBUF] = "this is  a test from Lind client \n";
	char recv_buf[MAXBUF];
	struct sockaddr_in addr;

	/* Create an AF_INET stream socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		fprintf(stderr, "client - socket() error \n");
		return NULL;
	}

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(LISTEN_SERVER_PORT);


	rc = connect(sockfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));
	if (rc < 0) {
		fprintf(stderr, "client - connect() error \n");
		return NULL;
	}

	len = send(sockfd, send_buf, strlen(send_buf) + 1, 0);

	if (len != strlen(send_buf) + 1) {
		fprintf(stderr, "client - send() error \n");
		return NULL;
	}

	fprintf(stdout, "%d bytes sent \n", len);
	len = recv(sockfd, recv_buf, sizeof(recv_buf), 0);

	if (len != strlen(send_buf) + 1) {
		fprintf(stderr, "client - recv() error \n");
		return NULL;
	}

	fprintf(stdout, "server %s \n", recv_buf);
	fprintf(stdout, "%d bytes received \n", len);

	if (close(sockfd)!= 0){
		fprintf(stderr, "close() error \n");
		return NULL;
	}
	return (void*) 1;
}
