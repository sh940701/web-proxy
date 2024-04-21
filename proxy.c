// https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/

#include <stdio.h>
#include "./csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8080
/* Misc constants */
#define    MAXLINE     8192  /* Max text line length */
#define MAXBUF   8192  /* Max I/O buffer size */
#define LISTENQ  1024  /* Second argument to listen() */

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
        "Firefox/10.0.3";

void deliver(int);

void request_to_server(int, char *, int);

void generate_header(char *, char *, char *, rio_t *);

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        deliver(connfd);
        Close(connfd);
    }
}

void deliver(int connfd) {
    char buf[MAXLINE], method[MAXLINE], path[MAXLINE], *received_data[MAXBUF];
    struct sockaddr_in servaddr;
    int clientfd;
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
//    printf("Proxy Request headers:\n");
//    printf("%s", buf);

    // browser 에서 요청을 보낼 때, 실제로는 host 와 uri 를 따로 보낸다: http://localhost/index.html X -> /index.html
    // 따라서 path 만 포함하기 위해 따로 구현해 줄 사항이 없다.
    sscanf(buf, "%s %s", method, path);

    // proxy_client socket 생성 및 verification
    clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1) {
        printf("socket creation failed...\n");
        return;
    }

    // servaddr 구조체 초기화
    memset(&servaddr, 0, sizeof(servaddr));
    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(SERVER_HOST);
    servaddr.sin_port = htons(SERVER_PORT); // network byte 순서를 big endian 순서로 하기 위한 htons 함수

    // client - server connect
    if (connect(clientfd, (SA *) &servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        return;
    }

    // 서버로 요청 전송 및 응답 데이터 저장
    request_to_server(clientfd);


    // 요청 및 데이터 전달 완료 후 clientfd close
    close(clientfd);
}
