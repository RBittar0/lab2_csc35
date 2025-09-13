#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>

#define SERVER_PORT 12345
#define BUF_SIZE    4096
#define QUEUE_SIZE  10

void fatal(char *string);
ssize_t write_all(int fd, const void *buf, size_t n);
ssize_t read_line(int fd, char *out, size_t max);
void *session(void *arg);

int main(int argc, char **argv)
{
    int s, b, l, fd, sa, bytes, on = 1;
    char buf[BUF_SIZE];
    struct sockaddr_in channel;

    /* Monta estrutura de enderecos para vincular ao soquete local. */
    memset(&channel, 0, sizeof(channel));
    channel.sin_family = AF_INET;
    channel.sin_addr.s_addr = htonl(INADDR_ANY);
    channel.sin_port = htons(SERVER_PORT);

    /* Abertura passiva. Espera a conexao. */
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) fatal("socket failed");
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));

    b = bind(s, (struct sockaddr *)&channel, sizeof(channel));
    if (b < 0) fatal("bind failed");

    l = listen(s, QUEUE_SIZE);
    if (l < 0) fatal("listen failed");

    /* O soquete agora esta preparado e vinculado. Espera conexao e atende. */
    while (1) {
        int *sa_ptr = malloc(sizeof(int));
        if (!sa_ptr) fatal("malloc failed");
        *sa_ptr = accept(s, 0, 0);
        if (*sa_ptr < 0) { free(sa_ptr); fatal("accept failed"); }

        pthread_t th;
        if (pthread_create(&th, NULL, session, sa_ptr) != 0) {
            perror("pthread_create");
            close(*sa_ptr);
            free(sa_ptr);
            continue;
        }
        pthread_detach(th);
    }
}

void fatal(char *string){
    printf("%s\n", string);
    exit(1);
}

ssize_t write_all(int fd, const void *buf, size_t n){
    const char *p = buf;
    size_t left = n;
    while(left>0){
        ssize_t w = write(fd, p, left);
        if(w<0) return w;
        if(w==0) break;
        p += w;
        left -= w;
    }
    return (ssize_t)(n-left);
}

ssize_t read_line(int fd, char *out, size_t max){
    size_t used = 0;
    while(used + 1 < max){
        char c;
        ssize_t r = read(fd, &c, 1);
        if(r == 0) break;
        if(r < 0) return -1;
        out[used++] = c;
        if(c == '\n') break;
    }
    out[used] = '\0';
    return (ssize_t)used;
}

void *session(void *arg){
    int sa = *(int*)arg;
    free(arg);
    printf("Session started!\n");
    
    time_t last_access = 0;
    char line[BUF_SIZE];

    while(1){
        ssize_t ln = read_line(sa, line, sizeof(line));
        if(ln <= 0) break;
        if(ln == 1 && line[0] == '\n') continue;

        char *p = line, *nl = strpbrk(p, "\r\n");
        if(nl) *nl = '\0';

        if(strncmp(p, "MyGet ", 6) == 0){
            const char *path = p+6;
            while(*path == ' ') path++;

            int fd = open(path, O_RDONLY);
            if (fd < 0) {
                dprintf(sa, "Error: open failed\r\n");
                continue; 
            }

            struct stat st;
            if(fstat(fd, &st) < 0) {
                dprintf(sa, "Error: stat failed\r\n");
                close(fd);
                continue;
            }

            char header[256];
            int hn = snprintf(header, sizeof(header), "MyOK\r\nContent-Length: %lld\r\n\r\n", (long long)st.st_size);

            write_all(sa, header, hn);

            char buf[BUF_SIZE];
            ssize_t bytes;
            while((bytes = read(fd, buf, BUF_SIZE)) > 0){
                if (write_all(sa, buf, (size_t)bytes) < 0) { bytes = -1; break; }
            }

            close(fd);
            if (bytes < 0) break;

            last_access = time(NULL);
        } else if( strncmp(p, "MyLastAccess", 12) == 0) {
            char payload[128];
            
            if (last_access == 0) {
                strcpy(payload, "Last-Access: Null\n");
            } else {
                struct tm tmv;
                localtime_r(&last_access, &tmv);
                strftime(payload, sizeof(payload), "Last-Access: %Y-%m-%d %H:%M:%S \n", &tmv);
            }
            
            char header[128];
            int blen = (int)strlen(payload);
            int hn = snprintf(header, sizeof(header),  "MyOK\r\nContent-Length: %d\r\n\r\n", blen);
            
            if (write_all(sa, header, hn) < 0) break;
            if (write_all(sa, payload, blen) < 0) break;
        } else {
            dprintf(sa, "Error: unknown method\r\n");
        }
    } 

    close(sa);
    return NULL;
}