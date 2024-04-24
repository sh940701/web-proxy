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

void serve_static(int fd, char *filename, int filesize, char *method);

void get_filetype(char *filename, char *filetype);

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

// 실행 arg 로 port number 입력
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
    printf("Server Request headers:\n");
    printf("%s", buf);
    // 11.6 - c
    // GET /cgi-bin/adder?130&19 HTTP/1.1
    // buf 에 method, uri, version 값이 위와 같이 담긴다.
    // 이를 통해 현재 browser 에서 사용하는 HTTP version 이 1.1 이라는 것을 알 수 있다.
    sscanf(buf, "%s %s %s", method, uri, version); // buf 에서 세 개의 문자열을 받아서 method, uri, version 변수에 저장하는 함수
    // 11-11
    if (strcasecmp(method, "GET") &&
        (strcasecmp(method, "HEAD"))) { // 대소문자 구분없이 문자열을 비교하는 함수. 두 값이 같으면 0을 반환하기 때문에 method == get/GET 인 경우 통과한다.
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }
    read_requesthdrs(&rio);

    // 정적 파일 API 인지, 동적 API 인지 파악
    // html 을 받은 후, html 내부 태그에 따라서 실제로 요청을 더 보낸다 (video 혹은 img 혹은 both 의 데이터를 요구하기 위한 요청) GET /godzilla.gif 등
    // 이 때는 html 이 아닌 다른 타입(mpg, jpg 등) 으로 filename 이 설정되어, serve_static 내부의 get_filetype 을 통해 각각 요청한 파일 타입에 맞는 header 를 return 한다.
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
        serve_static(fd, filename, sbuf.st_size, method);
    }
        // dynamic content handling
    else {
        // 만약 stat 에 정의된 regular file 이 아니거나, 해당 파일에 대한 실행 권한이 없다면
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs, method);
    }

    memset()
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
//void serve_static(int fd, char *filename, int filesize) {
//    int srcfd;
//    char *srcp, filetype[MAXLINE], buf[MAXBUF];
//
//    // response header 전송
//    get_filetype(filename, filetype); // 파일 타입 확인
//    sprintf(buf, "HTTP/1.0 200 OK\r\n");
//    sprintf(buf, "%sServer: Tiny Web server \r\n", buf);
//    sprintf(buf, "%sConnection: close\r\n", buf);
//    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
//    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
//    Rio_writen(fd, buf, strlen(buf));
//    printf("Response headers: \n");
//    printf("%s", buf);
//
//    // response body 전송
//    // 파일 이름을 가지고 파일을 연다. 이 때 flag 로 읽기 권한을 부여하고, 읽기 권한 부여시 mode 는 중요하지 않기 때문에 0 으로 설정해준다.
//    // 파일 열기에 성공시 file descriptor 를 반환한다. file descriptor 는 운영체제가 어떤 열려있는 파일을 가리킬 수 있는 추상화된 정수값의 데이터이다.
//    srcfd = Open(filename, O_RDONLY, 0);
//    // Mmap: 파일이나 장치의 일부 혹은 전체를 메모리에 매핑하는 시스템 호출
//    // srcfd 에 의해 참조된 파일을 프로세스의 주소 공간에 매핑한다.
//    // 0: 시작주소, filesize: 파일 크기, PROT_READ: 매핑된 메모리 영역에 대한 보호 수준(READ_ONLY)
//    // MAP_PRIVATE: 매핑의 종류 - 쓰기 시 복사(매핑된 메모리에 대한 변경사항이 실제 파일에 반영되지 않고, 해당 프로세스에만 영향을 미침)
//    // srcfd: 매핑할 파일의 파일 디스크립터, 0: 파일 내에서 매핑을 시작할 오프셋, 이 경우 파일의 시작부터 매핑을 시작함.
//    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
//    // 파일을 srcp 에 복사했으므로, file descriptor 는 닫아준다.
//    Close(srcfd);
//    Rio_writen(fd, srcp, filesize);
//    // mmap 을 통해 메모리에 매핑된 파일 또는 장치의 일부분을 메모리에서 해제하는 시스템 호출
//    Munmap(srcp, filesize);
//}

// 11-9
void serve_static(int fd, char *filename, int filesize, char *method) {
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
    // 만약 method 가 HEAD 라면 header 만 보내고 return
    if (!strcasecmp(method, "HEAD")) {
        return;
    }

    // 파일을 열어서 file descriptor 를 만들어줌
    srcfd = Open(filename, O_RDONLY, 0);

    // 열어놓은 파일을 복사할 buffer 를 만들어줌 (동적 할당)
    srcp = (char *) malloc(filesize);

    // 파일을 fd 로부터 buffer 로 filesize 만큼 복사(read)
    Rio_readn(srcfd, srcp, filesize);

    // 파일을 buffer 로 부터 fd(client 로 향하는 파일 디스크립터) 로 filesize 만큼 복사 (write)
    Rio_writen(fd, srcp, filesize);

    // 정적 파일 디스크립터 close
    Close(srcfd);

    // 사용한 buffer free
    free(srcp);
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
    } else if (strstr(filename, ".mpg")) { // 11.7: mpg 타입을 위한 분류 추가
        strcpy(filetype, "video/mpeg");
    } else {
        strcpy(filetype, "text/plain");
    }
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
    char buf[MAXLINE], *emptylist[] = {NULL};

    // response header 의 첫 부분 return

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0) {
        setenv("QUERY_STRING", cgiargs, 1);
        setenv("METHOD", method, 1);
        Dup2(fd, STDOUT_FILENO);
        Execve(filename, emptylist, environ);
    }

    Wait(NULL);
}

//Dup2 를 사용해서 stdout 을 fd 로 변경해주는 이유가 뭐야? rio_write 함수를 사용해서 write 를 해도 client 에 데이터를 전송할 수 있는 것 아니야?
//Fork 를 하는 이유를 잘 모르겠어. 왜 굳이 부모 프로세스와 다른 하나의 프로세스를 생성해서 실행하는거야? 자식 프로세스의 개념도 어려운 것 같아
//Execve 를 사용하여 지정된 프로그램으로 대체한다고 했는데, 그러면 부모 프로세스를 대체하는거야 아니면 자식 프로세스를 대체하는거야? 그리고 프로세스를 대체하는 이유는 뭐야?
//
//Dup2를 사용해서 stdout을 fd로 변경하는 이유: dup2 함수를 사용하여 stdout을 fd(클라이언트와의 연결을 나타내는 파일 디스크립터)로 변경하는 이유는, 실행되는 CGI 프로그램이나 스크립트가 생성하는 출력을 직접 클라이언트에 전송하기 위함입니다. CGI 프로그램은 일반적으로 결과를 stdout으로 출력합니다. 서버가 이 출력을 캡처하여 클라이언트에 전송하기 위해서는 서버의 stdout을 클라이언트 연결의 파일 디스크립터로 리다이렉트해야 합니다. 이렇게 하면 CGI 프로그램의 printf나 다른 출력 함수로 생성된 모든 출력이 직접 클라이언트로 전송됩니다. rio_writen 함수를 사용해서 직접 쓸 수도 있지만, CGI 프로그램이 서버 코드의 변경 없이 어떤 출력이든 직접 클라이언트에 전송할 수 있도록 하기 위한 것입니다.
//Fork를 하는 이유: fork 함수를 사용하여 부모 프로세스와 별도의 자식 프로세스를 생성하는 이유는, 동시성을 제공하고, 서버의 안정성을 높이기 위함입니다. fork로 생성된 자식 프로세스에서 CGI 스크립트를 실행함으로써, 부모 프로세스(서버)는 계속해서 다른 클라이언트의 요청을 받아 처리할 수 있습니다. 만약 자식 프로세스에서 오류가 발생하여 프로그램이 비정상 종료되더라도, 부모 프로세스에는 영향을 주지 않으므로 서버 자체의 안정성이 유지됩니다.
//Execve의 사용과 프로세스 대체: execve 함수는 호출한 프로세스(이 경우 자식 프로세스)를 지정된 새 프로그램으로 완전히 대체합니다. 즉, 부모 프로세스는 그대로 남아 있고, 자식 프로세스만이 지정된 새 프로그램으로 대체됩니다. 이러한 대체는 자식 프로세스의 실행 컨텍스트를 변경하기 위해서 사용됩니다. 기존의 자식 프로세스가 실행 중이던 코드는 모두 제거되고, 지정된 파일의 새 코드가 메모리에 로드되어 실행됩니다. 이 방법을 사용함으로써, 동일한 프로세스 내에서 다른 프로그램을 실행할 수 있으며, 이는 CGI 프로그램을 실행하고 그 결과를 클라이언트에 전송하는 데 필요한 메커니즘입니다.
//정리하자면, dup2는 CGI 프로그램의 출력을 클라이언트에 직접 전송하기 위해, fork는 동시성과 안정성을 제공하기 위해, 그리고 execve는 자식 프로세스 내에서 새 프로그램을 실행하기 위해 사용됩니다.

// 그럼 위 코드에서 environ 은 대체 어디서 난거야 어떻게 정의되고 어떻게 쓰이는거야
// extern char *environ; / Defined by libc */
// 헤더 파일에 이렇게 정의되어있는 것 빼고는 어떻게 값이 생기고 어떻게 뭐가 되는건지 모르겠어

// environ은 C 언어에서 환경 변수를 다룰 때 사용하는 전역 변수입니다. 이 변수는 char ** 타입으로 선언되어 있으며, 환경 변수의 이름과 값이 이름=값 형태의 문자열로 이루어진 배열을 가리킵니다. 배열의 마지막 요소는 NULL로 끝납니다. 이를 통해 프로그램은 실행 환경에 대한 정보를 얻을 수 있습니다.

// environ 변수는 보통 운영 체제나 C 표준 라이브러리(libc)에 의해 자동으로 정의되고 초기화됩니다. 프로그램이 시작할 때, 운영 체제는 프로그램에게 실행 환경 정보를 전달하며, 이 정보는 environ 변수를 통해 접근할 수 있습니다.

// 예를 들어, 특정 환경 변수의 값을 읽거나, 환경 변수를 설정하는 함수(getenv, setenv, putenv 등)는 내부적으로 environ 변수를 사용하여 작업을 수행합니다.

// 위 코드에서 Execve함수를 호출할 때, 세 번째 인자로 environ을 전달하는 것은 CGI 스크립트나 프로그램이 현재 프로세스의 환경 변수를 그대로 사용할 수 있도록 하기 위함입니다. 즉, environ을 통해 현재의 환경 변수들을 새로 실행되는 프로그램에게 전달하여, 그 프로그램이 실행 환경에 대한 정보를 활용할 수 있게 해줍니다.

// 결론적으로, environ은 프로그램이 실행되는 환경에 대한 정보를 담고 있는 환경 변수 배열에 대한 포인터이며, 운영 체제에 의해 자동으로 정의되고 관리됩니다.
