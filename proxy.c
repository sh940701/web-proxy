// https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/

#include <stdio.h>
#include "./csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8080
#define STATIC_HTTP_VER "HTTP/1.0"
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
    char buf[MAXLINE], method[MAXLINE], path[MAXLINE], data_buf[MAX_OBJECT_SIZE], version[MAX_OBJECT_SIZE];
    struct sockaddr_in servaddr;
    int clientfd;
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);

    // browser 에서 요청을 보낼 때, 실제로는 host 와 uri 를 따로 보낸다: http://localhost/index.html X -> /index.html
    // 따라서 path 만 포함하기 위해 따로 구현해 줄 사항이 없다.
    sscanf(buf, "%s %s %s", method, path, version);

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

    generate_header(data_buf, method, path, &rio);

    // 서버로 요청 전송 및 응답 데이터 저장
    request_to_server(clientfd, data_buf, connfd);

    // 서버로부터 받은 data 를 client 에 전송
    Rio_writen(connfd, data_buf, MAX_OBJECT_SIZE);

    // 요청 및 데이터 전달 완료 후 clientfd close
    Close(clientfd);
}

// header 를 만들어주는 generate_header 함수
void generate_header(char *buf, char *method, char *path, rio_t *rp) {
    char tmp_buf[MAXLINE];

    memset(tmp_buf, 0, MAXLINE);

    sprintf(buf, "%s %s %s\r\n", method, path, STATIC_HTTP_VER);
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);

    while (strcmp(tmp_buf, "\r\n")) {
        Rio_readlineb(rp, tmp_buf, MAXLINE);
        if (strcasestr(tmp_buf, "GET") || strcasestr(tmp_buf, "HEAD") || strcasestr(tmp_buf, "User-Agent") ||
            strcasestr(tmp_buf, "Connection") || strcasestr(tmp_buf, "Proxy-Connection")) {
            continue;
        }
        strcat(buf, tmp_buf);
    }

    strcat(buf, "\r\n");
}

// server 로 request 를 보내는 request_to_server 함수
void request_to_server(int clientfd, char *buf, int connfd) {
    ssize_t n;

    Rio_writen(clientfd, buf, MAXLINE);

    printf("\nrequest header2 : \n%s", buf);

    memset(buf, 0, MAXLINE);

    Rio_readn(clientfd, buf, MAX_OBJECT_SIZE);

    if (n < 0) {
        // todo read error
    }
}
