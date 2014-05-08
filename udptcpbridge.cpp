/*
** UDP to TCP Bridge software
** Small program to make a reliable link
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#include <pthread.h>


#include <vector>
#include <string>


#define UDPPORT "3333"    // the port users will be connecting to
#define TCPPORT 3000

#define MAXBUFLEN 200

pthread_mutex_t mylock;
std::vector<std::vector<unsigned char> > pktqueue;
int connected = 0;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *TCPserver(void *arg)                    /* servlet thread */
{
	struct sockaddr_in addr;
	struct sockaddr_storage their_addr; // connector's address information
	int sd;
	int err = 0;
	int ret = 0;

	sd = socket(PF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		fprintf(stderr, "TCP: cannot create socket!\n");
		exit(-1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(TCPPORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	if ((ret = bind(sd, (struct sockaddr *)&addr, sizeof(addr))) != 0) {
		fprintf(stderr, "TCP: ERR bind %d\n", ret);
		err = 1;
	}

	if ((ret = listen(sd, 10)) != 0) {
		fprintf(stderr,"TCP: ERR listen %d", ret);
		err = 1;
	}

	while (!err) {
		socklen_t sin_size = sizeof their_addr;
		printf("TCP: Waiting for connection (IP:%d)...\n", TCPPORT);
        	int new_fd = accept(sd, (struct sockaddr *)&their_addr, &sin_size);
		printf("TCP: connected! %d\n", new_fd);
		connected = 1;

		while(connected) { 
			size_t queuesize = 0;
			
			/* Get queue size (TODO: this should use a semaphore and block) */
			pthread_mutex_lock (&mylock);
			queuesize = pktqueue.size();
			pthread_mutex_unlock(&mylock);


			/* extract from the queue and send it to the TCP client */
			while (queuesize > 0 && connected) {
				pthread_mutex_lock (&mylock);
				for (uint32_t z=0; z<pktqueue.size() && connected; z++) {
					if ((ret = send(new_fd, &(pktqueue[z][0]), pktqueue[z].size(), 0)) < 0) {
						if (errno == 32) {
							fprintf(stderr, "TCP: client disconnected\n");
						} else {
							fprintf(stderr, "TCP: ERR send (%d, %d)\n", errno, ret);
						}
						connected = 0;
						errno = 0;
					}
				}
				for (uint32_t z=0; z<pktqueue.size() && connected; z++) {
					pktqueue.pop_back();
					queuesize--;
				}
				pthread_mutex_unlock (&mylock);
			}
			usleep(10);
		}
	}

	printf("SERVER CLOSE\n");

	return(0);
}

int main(void)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv, numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];
    int queuelen = 0;
    pthread_t child;

    /* Ignore SIGPIPE (when client closes TCP connection and we write to it) */
    signal(SIGPIPE, SIG_IGN); 

    /* Start UDP Listen Port */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, UDPPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    /* Start TCP server thread */
    pthread_create(&child, 0, TCPserver, 0);

    printf("UDP: waiting to recvfrom (IP:%s)...\n", UDPPORT);
    addr_len = sizeof their_addr;

    while (1) {
	if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
		(struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("recvfrom");
		exit(1);
	}

	printf("UDP: recv (%s) [%d]\n",
		inet_ntop(their_addr.ss_family,
		    get_in_addr((struct sockaddr *)&their_addr),
		    s, sizeof s), numbytes);
	buf[numbytes] = '\0';

	// put buffer onto queue for all other TCP clients to receive
	std::vector<unsigned char> newpkt((const unsigned char *)buf, (const unsigned char *)buf + numbytes);

	//printf("listener: packet contains \"%s\"\n", newpkt.c_str());
	pthread_mutex_lock (&mylock);
		if (connected) {
			pktqueue.push_back(newpkt);
			queuelen = (int)pktqueue.size();
		}
	pthread_mutex_unlock (&mylock);

	if (queuelen > 1)
		printf("QueueLen=%d\n", queuelen);

    }
    close(sockfd);

    return 0;
}
