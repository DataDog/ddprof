#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

void pretty_print(char c) {
  printf( isgraph(c) || isspace(c) ? "%c" : "//%o", c);
}

void listener_worker(int fd) {
  static char res[] = "HTTP/1.1 200 OK\r\n"
                      "Content-Length: 0\r\n"
                      "\r\n\r\n";
  while(1) {
    char buf[4096*16] = {0};
    int n = 0;
    if(0>=(n=recv(fd, &buf, 4096*8, 0))) { // TODO: EINTR
      close(fd);
      return;
    }
//    send(fd, &res, sizeof(res), 0);
    for(int i=0; i<n; i++)
      pretty_print(buf[i]);

    int wfd = open("./listener.dump", O_WRONLY|O_CREAT, 0777);
    ftruncate(wfd,0);
    write(wfd, buf, n);
    close(wfd);
  }
}

int main(int argc, char** argv) {
  int port = -1;
  if(1>=argc)               return printf("Need to specify a port\n"), -1;
  if(!(port=atoi(argv[1]))) return printf("Couldn't bind to %s\n", argv[1]), -1;

  // Bind
  int lfd;
  struct sockaddr_in sa = {
    .sin_family = AF_INET,
    .sin_port   = htons(port),
    .sin_addr   = (struct in_addr){INADDR_ANY}};
  if(-1 == (lfd = socket(AF_INET, SOCK_STREAM, 0))                       ||
     -1 == bind(lfd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) ||
     -1 == listen(lfd, 1000)) {
    return printf("Couldn't bind/listen to port %d\n", port), -1;
  }

  while(1) {
    struct sockaddr_in si = {0};
    int rfd = accept(lfd, NULL, 0);
    if(!fork())
      listener_worker(rfd);  // exits here
    close(rfd);              // hangup in parent
  }

  return 0;
}
