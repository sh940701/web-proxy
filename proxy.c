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

// 각 캐시 아이템
typedef struct CacheItem {
    char *key;
    char *value;
    ssize_t size;
    struct CacheItem *prev;
    struct CacheItem *next;
} CacheItem;

// 전체 캐시 풀
typedef struct Cache {
    ssize_t capacity;
    CacheItem *head;
    CacheItem *tail;
} Cache;

// cache_pool 생성
static Cache *cache_pool;


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
        "Firefox/10.0.3";

CacheItem *createCacheItem(char *key, char *value, ssize_t size);

Cache *initCache(void);

void removeCacheItem(Cache *cache, CacheItem *item);

void put_cache(Cache *cache, char *key, char *value, ssize_t size);

char *get_cache(Cache *cache, char *key);

int is_available_cache(char *data);

void *context_free(void *vargp, int clientfd, int connfd);

void *deliver(void *vargv);

void request_to_server(int, char *, ssize_t *);

void generate_header(char *, char *, char *, char *, rio_t *, char *);

void parse_uri(char *uri, char *request_ip, char *port, char *filename);

int main(int argc, char **argv) {
    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    cache_pool = initCache();

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
    char filename[MAXLINE], hostname[MAXLINE], port[MAXLINE], key[MAXLINE], head_header[MAXLINE], server_header[MAXLINE];
    char *cache_data;
    ssize_t cache_size;

    struct sockaddr_in servaddr;
    int clientfd;
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);

    printf("Request headers:\n");
    printf("%s", buf);


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

    // generate_header 에서는 GET 요청을 위한 헤더와 HEAD 요청을 위한 헤더를 생성함
    generate_header(data_buf, method, hostname, filename, &rio, head_header);

    strcat(key, hostname);
    strcat(key, filename);


    cache_data = get_cache(cache_pool, key);

    // 캐시에 있으면 그대로 반환
    if (cache_data != NULL) {
        Rio_writen(connfd, cache_data, MAX_OBJECT_SIZE);

        printf("\n%s %s%s cache Hit! Get From cache\n", method, hostname, filename);

        // 요청 및 데이터 전달 완료 후 clientfd/connfd close, 동적할당 된 vargp free
        return context_free(vargp, clientfd, connfd);
    }
    // ================= 캐시에 값이 있다면, 위에서 로직 종료 =================





    // ================= 캐시에 값이 없는 경우, 아래 코드를 실행하며 서버로부터 데이터 GET, 캐싱 로직 진행 =================




    // 캐시에 값이 없다면, 서버로부터 데이터를 불러옴
    printf("\n%s %s%s cache Miss! Get From Server\n", method, hostname, filename);

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


    // 1. server 와 connection 생성
    if (connect(clientfd, (SA *) &servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        return NULL;
    }


    // 2. server 에 HEAD 요청 전송
    // 이 때 HEAD 요청을 전송하는 이유는, 지금 불러오고자 하는 데이터가 캐싱 가능한 크기인지 content-length 를 통해서 확인하고자 함이다.
    Rio_writen(clientfd, head_header, MAXLINE);
    Rio_readn(clientfd, server_header, MAX_OBJECT_SIZE);
    Close(clientfd);


    // 3. HTTP 특성상 방금 전 HEAD 요청으로 인해 서버와의 connection 이 종료되었으므로, 다시 연결 생성
    clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(clientfd, (SA *) &servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        return NULL;
    }


    // 4-1. 캐시 불가능한 파일이라면, 용량이 큰 파일이라는 의미이므로, while 문을 사용해서 server 로 부터 받은 데이터를 그대로 client 로 지속적 전달해줌
    if (is_available_cache(server_header) == 0) {
        printf("\n %s cache unavailable\n ", server_header);

        Rio_writen(clientfd, data_buf, MAXLINE);

        memset(data_buf, 0, MAXLINE);

        while (Rio_readn(clientfd, data_buf, MAXLINE) > 0) {
            Rio_writen(connfd, data_buf, MAXLINE);
            memset(data_buf, 0, MAXLINE);
        }

        // 요청 및 데이터 전달 완료 후 clientfd/connfd close, 동적할당 된 vargp free
        return context_free(vargp, clientfd, connfd);
    }


    // 4-2 캐시 가능한 파일이라면, 아래에서 캐시를 위한 로직을 실행해 줌
    // 서버로 요청 전송 및 응답 데이터 저장
    request_to_server(clientfd, data_buf, &cache_size);

    // 5. 제대로 된 데이터가 들어왔는지 확인
    if (0 < cache_size) {
        // 데이터가 제대로 들어왔다면, cache 삽입
        put_cache(cache_pool, key, data_buf, cache_size);
    }


    // 6. 캐시를 마치고, 서버로부터 받은 data 를 client 에 전송
    Rio_writen(connfd, data_buf, MAX_OBJECT_SIZE);


    // 요청 및 데이터 전달 완료 후 clientfd/connfd close, 동적할당 된 vargp free
    return context_free(vargp, clientfd, connfd);
}

// 실행 컨텍스트를 마무리해주는 함수
void *context_free(void *vargp, int clientfd, int connfd) {
    free(vargp);
    Close(clientfd);
    Close(connfd);
    return NULL;
}

// header 를 만들어주는 generate_header 함수
void generate_header(char *buf, char *method, char *hostname, char *filename, rio_t *rp, char *head_header) {
    char tmp_buf[MAXLINE];

    memset(tmp_buf, 0, MAXLINE);

    sprintf(buf, "%s %s %s\r\n", method, filename, STATIC_HTTP_VER);
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);

    // head 요청을 위한 header 생성

    sprintf(head_header, "%s %s %s\r\n", "HEAD", filename, STATIC_HTTP_VER);
    sprintf(head_header, "%s%s\r\n", head_header, user_agent_hdr);
    sprintf(head_header, "%sConnection: close\r\n", head_header);
    sprintf(head_header, "%sProxy-Connection: close\r\n", head_header);

    int host_flag = 0;

    while (strcmp(tmp_buf, "\r\n")) {
        Rio_readlineb(rp, tmp_buf, MAXLINE);
        if (strcasestr(tmp_buf, "GET") || strcasestr(tmp_buf, "HEAD") || strcasestr(tmp_buf, "User-Agent") ||
            strcasestr(tmp_buf, "Connection") || strcasestr(tmp_buf, "Proxy-Connection")) {
            continue;
        } else if (strcasestr(tmp_buf, "HOST: ")) {
            host_flag++;
        }
        strcat(buf, tmp_buf);
        strcat(head_header, tmp_buf);
    }

    // 호스트가 없으면 hostname 을 추가해줌
    if (!host_flag) {
        sprintf(buf, "%sHost: %s", buf, hostname);
        sprintf(buf, "%sHost: %s", head_header, hostname);
    }

    strcat(buf, "\r\n");
}

// server 로 request 를 보내는 request_to_server 함수
void request_to_server(int clientfd, char *buf, ssize_t *data_size) {

    Rio_writen(clientfd, buf, MAXLINE);

    memset(buf, 0, MAXLINE);

    *data_size = Rio_readn(clientfd, buf, MAX_OBJECT_SIZE);
}

void parse_uri(char *uri, char *request_ip, char *port, char *filename) {
    /*
    uri 파싱 조건
    요청ip,filename,port,http_version
    filename과 port는 있을 수도있고 없을수도있음
    URI_examle= http://request_ip:port/path
    http://request_ip:port
    http://request_ip/
    */
    char *ip_ptr, *port_ptr, *filename_ptr;
    ip_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri + 1;
    port_ptr = strchr(ip_ptr, ':');
    filename_ptr = strchr(ip_ptr, '/');
    if (filename_ptr != NULL) {
        strcpy(filename, filename_ptr);
        *filename_ptr = '\0';
    } else
        strcpy(filename, "/");
    if (port_ptr != NULL) {
        strcpy(port, port_ptr + 1);
        *port_ptr = '\0';
    } else {
        strcpy(port, "80");
    }
    strcpy(request_ip, ip_ptr);
}


// 새로운 cache 를 생성하는 함수
CacheItem *createCacheItem(char *key, char *value, ssize_t size) {
    CacheItem *newItem = (CacheItem *) malloc(sizeof(CacheItem));
    newItem->value = (char *) malloc(size);
    newItem->key = strdup(key);

    memcpy(newItem->value, value, size);
    newItem->size = size;
    newItem->prev = NULL;
    newItem->next = NULL;
    return newItem;
}

// cache pool init 함수
Cache *initCache() {
    Cache *cache = (Cache *) malloc(sizeof(Cache));
    cache->capacity = MAX_CACHE_SIZE;
    cache->head = NULL;
    cache->tail = NULL;
    return cache;
}

// cache_pool 에서 특정 cache 를 삭제하는 함수
void removeCacheItem(Cache *cache, CacheItem *item) {
    if (item->prev != NULL) {
        item->prev->next = item->next;
    } else {
        cache->head = item->next;
    }
    if (item->next != NULL) {
        item->next->prev = item->prev;
    } else {
        cache->tail = item->prev;
    }

    cache->capacity += item->size;
    free(item->key);
    free(item->value);
    free(item);
}


// cache_pool 에 cache 를 넣어주는 함수
void put_cache(Cache *cache, char *key, char *value, ssize_t size) {
    CacheItem *newItem = createCacheItem(key, value, size);

    while (cache->capacity < size) {
        removeCacheItem(cache, cache->tail);
    }

    newItem->next = cache->head;
    if (cache->head != NULL) {
        cache->head->prev = newItem;
    }
    cache->head = newItem;
    if (cache->tail == NULL) {
        cache->tail = newItem;
    }

    cache->capacity -= size;
}


// cache_pool 에서 특정 cache 를 가져오는 함수
char *get_cache(Cache *cache, char *key) {
    CacheItem *curr = cache->head;

    while (curr != NULL) {
        if (strcmp(curr->key, key) == 0) {
            // 해당 항목을 가장 최근에 사용했으므로, head 로 옮겨줌
            if (curr != cache->head) {
                curr->prev->next = curr->next;
                if (curr->next != NULL) {
                    curr->next->prev = curr->prev;
                } else {
                    cache->tail = curr->prev;
                }
                curr->next = cache->head;
                curr->prev = NULL;
                cache->head->prev = curr;
                cache->head = curr;
            }
            return curr->value;
        }
        curr = curr->next;
    }

    return NULL;
}


// header 에서 content-length: 를 읽어서, 캐싱 가능한 데이터인지 확인해주는 함수
int is_available_cache(char *data) {
    char *content_length_start = strcasestr(data, "Content-Length: "); // "Content-Length: " 문자열 찾기
    // "Content-Length: " 다음의 문자열에서 숫자를 읽어옴
    int content_length;
    sscanf(content_length_start + strlen("Content-Length: "), "%d", &content_length);

    // content_length와 MAX_OBJECT_SIZE를 비교하여 캐시의 크기 제한을 확인
    if (content_length <= MAX_OBJECT_SIZE) {
        return 1;
    } else {
        return 0;
    }
}
