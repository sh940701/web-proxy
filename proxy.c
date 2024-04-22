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
    int size;
    struct CacheItem *prev;
    struct CacheItem *next;
} CacheItem;

// 전체 캐시 풀
typedef struct Cache {
    int size;
    int capacity;
    CacheItem *head;
    CacheItem *tail;
} Cache;

static Cache *cache_pool;


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
        "Firefox/10.0.3";

CacheItem *createCacheItem(char *key, char *value, int size);

Cache *initCache(void);

void removeCacheItem(Cache *cache, CacheItem *item);

void put_cache(Cache *cache, char *key, char *value, int size);

char *get_cache(Cache *cache, char *key);

int is_available_cache(char *data);


void *deliver(void *vargv);

void request_to_server(int, char *, int);

void generate_header(char *, char *, char *, char *, rio_t *);

void parse_uri(char *uri, char *request_ip, char *port, char *filename);

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
    char filename[MAXLINE], hostname[MAXLINE], port[MAXLINE], key[MAXLINE];
    char *cache_data;
    int cache_size;

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

    strcat(key, hostname);
    strcat(key, filename);

    cache_data = get_cache(cache_pool, key);

    // 캐시에 있으면 그대로 반환
    if (cache_data != NULL) {
        Rio_writen(connfd, cache_data, MAX_OBJECT_SIZE);

        printf("\n%s %s%s cache Hit! Get From cache\n", method, hostname, filename);

        free(vargp);
        // 요청 및 데이터 전달 완료 후 clientfd, connfd close
        Close(clientfd);
        Close(connfd);
        return NULL;
    }

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

    // client - server connect
    if (connect(clientfd, (SA *) &servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        return NULL;
    }

    // 서버로 요청 전송 및 응답 데이터 저장
    request_to_server(clientfd, data_buf, connfd);

    cache_size = is_available_cache(data_buf);
    // 캐싱 가능한 대상인지 확인 후, 캐싱 가능하다면 캐시에 추가해줌
    if (cache_size) {
        put_cache(cache_pool, key, data_buf, cache_size);
    }

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


// 새로운 캐시 항목을 생성하는 함수
CacheItem *createCacheItem(char *key, char *value, int size) {
    CacheItem *newItem = (CacheItem *) malloc(sizeof(CacheItem));
    newItem->key = strdup(key);
    newItem->value = strdup(value);
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

// 캐시에서 특정 항목을 삭제하는 함수
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

void put_cache(Cache *cache, char *key, char *value, int size) {
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

// 캐시에서 특정 항목을 가져오는 함수
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

int is_available_cache(char *data) {
    char *content_length_start = strcasestr(data, "Content-Length: "); // "Content-Length: " 문자열 찾기
    // "Content-Length: " 다음의 문자열에서 숫자를 읽어옴
    int content_length;
    sscanf(content_length_start + strlen("Content-Length: "), "%d", &content_length);

    // content_length와 MAX_OBJECT_SIZE를 비교하여 캐시의 크기 제한을 확인
    if (content_length <= MAX_OBJECT_SIZE) {
        return content_length;
    } else {
        return 0;
    }
}
