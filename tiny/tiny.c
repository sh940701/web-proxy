/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);

void read_requesthdrs(rio_t *rp);

int parse_uri(char *uri, char *filename, char *cgiargs);

void serve_static(int fd, char *filename, int filesize);

void get_filetype(char *filename, char *filetype);

void serve_dynamic(int fd, char *filename, char *cgiargs);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

// 실행 arg 로 port number, listen socket fd 입력
int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);  // line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);   // line:netp:tiny:doit
        Close(connfd);  // line:netp:tiny:close
    }
}


void doit(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    // Read request line and headers
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version); // buf 에서 세 개의 문자열을 받아서 method, uri, version 변수에 저장하는 함수
    if (strcasecmp(method, "GET")) { // 대소문자 구분없이 문자열을 비교하는 함수. 두 값이 같으면 0을 반환하기 때문에 method == get/GET 인 경우 통과한다.
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }
    read_requesthdrs(&rio);

    // 정적 파일 API 인지, 동적 API 인지 파악
    is_static = parse_uri(uri, filename, cgiargs);

    // filename 에 명시된 파일의 존재여부, 메타데이터 등을 확인하여, valid 한 값이라면 sbuf 에 그 파일 데이터를 채워줌
    if (stat(filename, &sbuf) < 0) {
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    // static content handling
    if (is_static) {
        // 만약 stat 에 정의된 regular file 이 아니거나, 해당 파일에 대한 읽기 권한이 없다면
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
            return;
        }
        serve_static(fd, filename, sbuf.st_size);
    }
        // dynamic content handling
    else {
        // 만약 stat 에 정의된 regular file 이 아니거나, 해당 파일에 대한 실행 권한이 없다면
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs);
    }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int) strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}


// request header 를 읽고 무시
void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    // todo Rio_readlineb 가 내부적으로 어떤 일을 하길래 한 번 실행하고, while 문에서 line 별로 실행하는지 파악 필요
    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;

    // cgi-bin 이라는 문자열이 포함되면 동적 요청
    if (!strstr(uri, "cgi-bin")) { // -> 정적 요청
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        if (uri[strlen(uri) - 1] == '/') {
            strcat(filename, "home.html");
        }
        return 1;
    } else { // -> 동적 요청
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        } else {
            strcpy(cgiargs, "");
        }
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }
}

// 이 함수를 호출하는 시점에는, 아직 파일을 가상메모리에 올리기 전이다.
void serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    // response header 전송
    get_filetype(filename, filetype); // 파일 타입 확인
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web server \r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers: \n");
    printf("%s", buf);

    // response body 전송
    // 파일 이름을 가지고 파일을 연다. 이 때 flag 로 읽기 권한을 부여하고, 읽기 권한 부여시 mode 는 중요하지 않기 때문에 0 으로 설정해준다.
    // 파일 열기에 성공시 file descriptor 를 반환한다. file descriptor 는 운영체제가 어떤 열려있는 파일을 가리킬 수 있는 추상화된 정수값의 데이터이다.
    srcfd = Open(filename, O_RDONLY, 0);
    // Mmap: 파일이나 장치의 일부 혹은 전체를 메모리에 매핑하는 시스템 호출
    // srcfd 에 의해 참조된 파일을 프로세스의 주소 공간에 매핑한다.
    // 0: 시작주소, filesize: 파일 크기, PROT_READ: 매핑된 메모리 영역에 대한 보호 수준(READ_ONLY)
    // MAP_PRIVATE: 매핑의 종류 - 쓰기 시 복사(매핑된 메모리에 대한 변경사항이 실제 파일에 반영되지 않고, 해당 프로세스에만 영향을 미침
    // srcfd: 매핑할 파일의 파일 디스크립터, 0: 파일 내에서 매핑을 시작할 오프셋, ㅣ 경우 파일의 시작부터 매핑을 시작함.
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    // 파일을 srcp 에 복사했으므로, file descriptor 는 닫아준다.
    Close(srcfd);
    // mmap 을 통해 메모리에 매핑된 파일 또는 장치의 일부분을 메모리에서 해제하는 시스템 호출
    Munmap(srcp, filesize);
}

void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) {
        strcpy(filetype, "text/html");
    } else if (strstr(filename, ".gif")) {
        strcpy(filetype, "image/gif");
    } else if (strstr(filename, ".png")) {
        strcpy(filetype, "image/png");
    } else if (strstr(filename, ".jpg")) {
        strcpy(filetype, "image/jpeg");
    } else {
        strcpy(filetype, "text/plain");
    }
}
