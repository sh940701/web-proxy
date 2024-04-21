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

void *deliver(void *vargv);

void request_to_server(int, char *, int);

void generate_header(char *, char *, char *, char *, rio_t *);

void parse_uri(char *uri, char *hostname, char *port, char *filename);

void print_log(char *desc, char *text) {
    FILE *fp = fopen("output.log", "a");

    fprintf(fp, "====================%s====================\n%s", desc, text);

    if (text[strlen(text) - 1] != '\n')
        fprintf(fp, "\n");

    fclose(fp);
}

int main(int argc, char **argv) {
    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = malloc(sizeof(int));

        *connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);

        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        pthread_create(&tid, NULL, deliver, connfd);
    }
}

void *deliver(void *vargp) {
    int connfd = *((int *) vargp);
    pthread_detach(pthread_self());

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], data_buf[MAX_OBJECT_SIZE], version[MAX_OBJECT_SIZE];
    char filename[MAXLINE], hostname[MAXLINE], port[MAXLINE];

    struct sockaddr_in servaddr;
    int clientfd;
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);

    printf("Request headers:\n");
    printf("%s", buf);
    print_log("\n날아온 헤더\n", buf);


    // browser 에서 요청을 보낼 때, 실제로는 host 와 uri 를 따로 보낸다: http://localhost/index.html X -> /index.html
    // 따라서 path 만 포함하기 위해 따로 구현해 줄 사항이 없다.
    sscanf(buf, "%s %s %s", method, uri, version);

    parse_uri(uri, hostname, port, filename);

    // proxy_client socket 생성 및 verification
    clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1) {
        printf("socket creation failed...\n");
        return NULL;
    }

    generate_header(data_buf, method, hostname, filename, &rio);

    print_log("생성된 헤더\n", data_buf);

    // localhost 는 127.0.0.1 로 변경
    if (strcmp(hostname, "localhost") == 0) {
        strcpy(hostname, SERVER_HOST);
    }

    // servaddr 구조체 초기화
    memset(&servaddr, 0, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(hostname);
    servaddr.sin_port = htons(atoi(port)); // network byte 순서를 big endian 순서로 하기 위한 htons 함수

    // client - server connect
    if (connect(clientfd, (SA *) &servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        return NULL;
    }

    // 서버로 요청 전송 및 응답 데이터 저장
    request_to_server(clientfd, data_buf, connfd);

    // 서버로부터 받은 data 를 client 에 전송
    Rio_writen(connfd, data_buf, MAX_OBJECT_SIZE);

    free(vargp);
    // 요청 및 데이터 전달 완료 후 clientfd, connfd close
    Close(clientfd);
    Close(connfd);

    return NULL;
}

// header 를 만들어주는 generate_header 함수
void generate_header(char *buf, char *method, char *hostname, char *filename, rio_t *rp) {
    char tmp_buf[MAXLINE];

    memset(tmp_buf, 0, MAXLINE);

    sprintf(buf, "%s %s %s\r\n", method, filename, STATIC_HTTP_VER);
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);

    int host_flag = 0;

    while (strcmp(tmp_buf, "\r\n")) {
        Rio_readlineb(rp, tmp_buf, MAXLINE);
        print_log("날아온 헤더들2", tmp_buf);
        if (strcasestr(tmp_buf, "GET") || strcasestr(tmp_buf, "HEAD") || strcasestr(tmp_buf, "User-Agent") ||
            strcasestr(tmp_buf, "Connection") || strcasestr(tmp_buf, "Proxy-Connection")) {
            continue;
        } else if (strcasestr(tmp_buf, "HOST: ")) {
            host_flag++;
        }
        strcat(buf, tmp_buf);
    }

    // 호스트가 없으면 hostname 을 추가해줌
    if (!host_flag) {
        sprintf(buf, "%sHost: %s", buf, hostname);
    }

    strcat(buf, "\r\n");
}

// server 로 request 를 보내는 request_to_server 함수
void request_to_server(int clientfd, char *buf, int connfd) {
    ssize_t n;

    Rio_writen(clientfd, buf, MAXLINE);

    memset(buf, 0, MAXLINE);

    Rio_readn(clientfd, buf, MAX_OBJECT_SIZE);

    if (n < 0) {
        // todo read error
    }
}

//URI 예시 http://127.0.0.1:8000/htmls/index.html
void parse_uri(char *uri, char *hostname, char *port, char *filename) {
    char *p;
    char arg1[MAXLINE], arg2[MAXLINE];

    if (p = strchr(uri, '/')) //문자열 uri 내에 일치하는 문자 '/' 가 있는지 검사하는 함수. 있으면 '/'를 가리키는 포인터 리턴
    { // http:   여기가 p-->//127.0.0.1:8000/htmls/index.html
        sscanf(p + 2, "%s", arg1); // 위에서 p+2 하면 127...부터 읽어와 arg1에 저장 => ex)127.0.0.1:8000/htmls/index.html
    } else {
        strcpy(arg1, uri); //문자열 내에 '/' 가 없으면 uri전체를 arg1에 저장 (무조건 들어오긴함)
    }

    if (p = strchr(arg1, ':')) { //문자열 uri 내에 일치하는 문자 ':' (포트번호) 가 있는지 검사하는 함수. 있으면 ':'를 가리키는 포인터 리턴
        // 127.0.0.1   여기가 p-->:8000/htmls/index.html
        *p = '\0'; // :를 \0으로 바꿈 -> arg1은 이제 127.0.0.1 이 됨
        sscanf(arg1, "%s", hostname); // arg1을 hostname에 저장  => hostname 은 127.0.0.1
        sscanf(p + 1, "%s", arg2); //포트(와 그 뒤)를 arg2에 저장 => arg2 는 8000/htmls/index.html

        p = strchr(arg2, '/'); //arg2 즉 포트 뒤에 '/'가 있다면 '/'를 가리키는 포인터를 p에 저장 => 8000  여기-->/  htmls/index.html
        *p = '\0'; // /를 \0으로 바꿈 --> arg2는 이제 8000이 됨
        sscanf(arg2, "%s", port); //port에 8000 을 저장함
        *p = '/'; // 여기가 p-->/htmls/index.html
        sscanf(p, "%s", filename); //filename은 /htmls/index.html 가 됨.

    } else { //문자열 uri 내에 포트번호가 없다면 ex) uri가 http://127.0.0.1/htmls/index.html 라면 160~167줄을 통해 arg1은 127.0.0.1/htmls/index.html
        p = strchr(arg1, '/'); //  127.0.0.1  여기가 p-->/htmls/index.html
        *p = '\0'; //arg1에서 '/' 를 없애고 => 127.0.0.1 \0 htmls/index.html => arg1은 127.0.0.1 이 됨
        sscanf(arg1, "%s", hostname); //ag1을 hostname에 저장. hostname 은 127.0.0.1
        *p = '/';
        sscanf(p, "%s", filename); // filename은 /htmls/index.html 가 됨.

    }
}
