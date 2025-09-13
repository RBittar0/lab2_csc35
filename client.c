#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   
#include <fcntl.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define SERVER_PORT 12345
#define BUF_SIZE    4096

void fatal(char *string);
ssize_t write_all(int fd, const void *buf, size_t n);
ssize_t read_line(int fd, char *out, size_t max);
ssize_t read_n(int fd, void *buf, size_t n);
int read_status_and_headers(int s, long *out_cl);
int handle_response(int s, int outfd);

int main(int argc, char **argv)
{
    int c, s;
    struct hostent *h;
    struct sockaddr_in channel;

    if (argc < 2 || argc > 4) {
        fprintf(stderr,
            "Usage:\n"
            "  %s <host>                    # modo interativo\n"
            "  %s <host> <path>             # MyGet <path> (1-shot)\n"
            "  %s <host> <path> <outfile>   # MyGet -> salva em arquivo\n"
            "  %s <host> LAST               # MyLastAccess (1-shot)\n",
            argv[0], argv[0], argv[0], argv[0]);
        exit(1);
    }


    h = gethostbyname(argv[1]);            /* pesquisa endereco IP do host */
    if (!h) fatal("gethostbyname failed");

    s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) fatal("socket");

    memset(&channel, 0, sizeof(channel));
    channel.sin_family = AF_INET;
    memcpy(&channel.sin_addr.s_addr, h->h_addr, h->h_length);
    channel.sin_port = htons(SERVER_PORT);

    c = connect(s, (struct sockaddr *)&channel, sizeof(channel));
    if (c < 0) fatal("connect failed");

    /* Modo sessão interativa: ./client <host> */
    if (argc == 2) {
        char cmd[BUF_SIZE];
        while (fgets(cmd, sizeof(cmd), stdin)) {
            if (strncmp(cmd, "quit", 4) == 0) break;
            if (write_all(s, cmd, strlen(cmd)) < 0) fatal("write request");
            int r = handle_response(s, 1);
            if (r < 0) fatal("handle_response");      
        }
        close(s);
        return 0;
    }

    /* Modo 1-shot: ./client <host> LAST  (MyLastAccess) */
    if (argc == 3 && strcmp(argv[2], "LAST") == 0) {
        const char *req = "MyLastAccess\n";
        if (write_all(s, req, strlen(req)) < 0) fatal("write request");
        int r = handle_response(s,1);
        if (r < 0) fatal("handle_response");
        close(s);
        return (r == 1 ? 0 : 2); // 0 ok, 2 erro do servidor
    }

    /* Modo 1-shot: ./client <host> <path> [outfile]  (MyGet) */
    {
        char req[BUF_SIZE];
        int rn = snprintf(req, sizeof(req), "MyGet %s\n", argv[2]);
        if (rn <= 0 || rn >= (int)sizeof(req)) fatal("snprintf request");
        if (write_all(s, req, rn) < 0) fatal("write request");

        int outfd = 1; 
        if (argc == 4) {
            outfd = open(argv[3], O_CREAT | O_TRUNC | O_WRONLY, 0644);
            if (outfd < 0) fatal("open outfile");
        }

        int r = handle_response(s, outfd);
        if (argc == 4 && outfd != 1) close(outfd);
        if (r < 0) fatal("handle_response");
        close(s);
        return (r == 1 ? 0 : 2); // 0 ok, 2 erro do servidor (Error:)
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

// Lê exatamente N bytes 
ssize_t read_n(int fd, void *buf, size_t n) {
    char *p = buf; 
    size_t left = n;
    while (left) {
        ssize_t r = read(fd, p, left);
        if (r < 0) return r; 
        if (r == 0) break; 
        p += r; 
        left -= r; 
    }
    return (ssize_t)(n - left);
}

int read_status_and_headers(int s, long *out_cl){
    char line[BUF_SIZE];
    ssize_t ln = read_line(s, line, sizeof(line));
    if(ln <= 0) return -1;

    if(strncmp(line, "MyOK", 4) == 0){
        long content_len = -1;
        while(1){
            ln = read_line(s, line, sizeof(line));
            if(ln <= 0) return -1;

            if(strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) break; // encontrou o fimda linha;

            if(strncmp(line, "Content-Length:", 15) == 0){
                const char *p = line + 15;
                while(*p == ' ' || *p == '\t' || *p == ':') p++;
                content_len = strtol(p, NULL, 10);
            }
        }

        if(content_len < 0) {
            fprintf(stderr, "missing Content-Length\n");
            return -1;
        }

        *out_cl = content_len;
        return 1;
    }

    if(strncmp(line, "Error:", 6) == 0){
        fprintf(stderr, "%s", line);
        return 0;
    }

    fprintf(stderr, "unexpected status: %s", line);
    return 0;
}

int handle_response(int s, int outfd) {
    long cl = -1;
    int st = read_status_and_headers(s, &cl);
    if (st <= 0) return st; // 0 = erro do servidor (linha "Error:"), -1 = falha

    long left = cl;
    char buf[BUF_SIZE];
    while (left > 0) {
        size_t chunk = (left > BUF_SIZE ? BUF_SIZE : (size_t)left);
        ssize_t r = read_n(s, buf, chunk);
        if (r <= 0) {
            fprintf(stderr, "connection closed early while reading body\n");
            return -1;
        }
        if (write_all(outfd, buf, (size_t)r) < 0) return -1;
        left -= r;
    }
    return 1;
}