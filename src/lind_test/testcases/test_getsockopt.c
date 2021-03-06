/*
 * test_getsockopt.c
 *
 *  Created on: Jun 20, 2014
 *      Author:  Ali Gholami
 *
 */

#include "testcases.h"

int main(int argc, char **argv)
{
	test_getsockopt();
	return 0;
}

void test_getsockopt()
{

	/* takedn from https://support.sas.com/documentation/onlinedoc/sasc/doc750/html/lr2/zsockopt.htm */

	int gs, socktype, sockfd;

	socklen_t optlen;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (sockfd == -1) {
		fprintf(stderr, "socket not created \n");
		return;
	}

	/* Ask for the socket type. */
	optlen = sizeof(socktype);
	gs = getsockopt(sockfd, SOL_SOCKET, SO_TYPE, (char *) &socktype, &optlen);

	if (gs == -1) {
		fprintf(stderr, "getsockopt() error \n");
		return;
	}

	switch (socktype) {

	case SOCK_STREAM:
		fprintf(stdout, "Stream socket.\n");
		break;
	case SOCK_DGRAM:
		fprintf(stdout, "Datagram socket.\n");
		break;
	case SOCK_RAW:
		fprintf(stdout, "Raw socket.\n");
		break;
	default:
		fprintf(stdout, "Unknown socket type.\n");
		break;
	}

	if (close(sockfd)!= 0){
	    fprintf(stderr, "close() error \n");
	   	return;
	}
}
