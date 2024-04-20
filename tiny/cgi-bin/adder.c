/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "../csapp.h"

int main(void) {
    char *buf, *p, *method;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
    int n1 = 0, n2 = 0;

    // getenv 는 stdlib 에서 지원해주는 내장함수
    // query 전체를 buf 에 복사
    if ((buf = getenv("QUERY_STRING")) != NULL) {
        // 주어진 문자열에서 지정된 문자를 찾아 반환하는 함수

        // '&' 문자의 위치를 찾음
        p = strchr(buf, '&');
        // '&' 문자를 '\0' 으로 변경. 따라서 buf 에서부터 문자열을 읽으면 & 전까지로 구분됨
        // '&' 이후 문자열은 p+1 로 접근할 수 있음
        *p = '\0';

        // 위 연산에 따라 두 개의 argument 를 변수에 cpy 해줌
        strcpy(arg1, buf);
        strcpy(arg2, p + 1);

        // 각 argument 문자열을 숫자로 변환
        n1 = atoi(arg1);
        n2 = atoi(arg2);
    }

    // response body 생성
    sprintf(content, "QUERY_STRING=%s", buf);
    sprintf(content, "Welcome to add.com: ");
    sprintf(content, "%sTHE Internet addition portal. \r\n<p>", content);
    sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
    sprintf(content, "%sThanks for visiting!\r\n", content);

    // HTTP response 생성
    // CGI 프로그램이기 때문에, 이전 layer 에서 출력을 network socket 으로 세팅해 놓았을 것이라고 가정.
    // 따라서 printf 의 실행결과는 network socket 에 출력되게 된다.
    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", (int) strlen(content));
    printf("Content-type: text/html\r\n\r\n"); // HTTP header 와 body 사이에는 두 줄이 비워져있어야 함
    // method 가 HEAD 이면 header 만 보내고 exit
    method = getenv("METHOD");
    if (!strcasecmp(method, "HEAD")) {
        fflush(stdout);
        exit(0);
    }
    printf("%s", content);

    // fflush 는 출력 스트림의 버퍼를 강제로 비우는 역할을 한다.
    // C언어에서 printf 같은 출력 함수들은 효율성을 위해 내부적으로 버퍼를 사용하고, 이는 즉시 데이터를 대상 장치나 파일에 쓰는 대신
    // 일정량의 데이터가 모일 때까지 버퍼에 저장하였다가 한 번에 전송하는 방식을 사용한다.
    // 그러나 때로 버퍼링 된 데이터를 즉시 전송해야 할 필요가 있을 때, fflush(stdout) 을 호출하여 표준 출력 버퍼에 있는 모든 데이터를 강제로 즉시 출력하게 한다.
    // fflush 를 사용하지 않아도 표준 출력은 client 에게 전달될 수 있다.
    // 그러나 즉시 출력 및 buffer 를 명시적으로 비우기 위해서 일반적인 네트워크 프로그래밍에서는 fflush 를 사용한다.
    fflush(stdout);

    exit(0);
}
/* $end adder */
