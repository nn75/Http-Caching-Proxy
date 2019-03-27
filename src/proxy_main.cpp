#include "proxy_server.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace std;

cache_list proxycache;

static void skeleton_daemon() {
  pid_t pid;

  pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE);

  if (pid > 0)
    exit(EXIT_SUCCESS);

  if (setsid() < 0)
    exit(EXIT_FAILURE);

  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);

  pid = fork();

  if (pid < 0)
    exit(EXIT_FAILURE);

  if (pid > 0)
    exit(EXIT_SUCCESS);

  umask(0);

  chdir("/");

  /*  int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        close (x);
    }*/

  openlog("the daemon", LOG_PID, LOG_DAEMON);
}

void proxy_work(int ID, int client_connection_fd, string socket_ip) {
  proxy_server PS(ID, client_connection_fd, socket_ip);
  PS.work(proxycache);
  //    return;
}

int main() {

  int status;
  int socket_fd;
  struct addrinfo host_info;
  struct addrinfo *host_info_list;
  const char *hostname = NULL;
  const char *port = "12345";

  memset(&host_info, 0, sizeof(host_info));

  host_info.ai_family = AF_UNSPEC;
  host_info.ai_socktype = SOCK_STREAM;
  host_info.ai_flags = AI_PASSIVE;

  status = getaddrinfo(hostname, port, &host_info, &host_info_list);
  if (status != 0) {
    cerr << "Error: cannot get address info for host" << endl;
    cerr << "  (" << hostname << "," << port << ")" << endl;
    return -1;
  } // if

  socket_fd = socket(host_info_list->ai_family, host_info_list->ai_socktype,
                     host_info_list->ai_protocol);
  if (socket_fd == -1) {
    cerr << "Error: cannot create socket" << endl;
    cerr << "  (" << hostname << "," << port << ")" << endl;
    return -1;
  } // if

  int yes = 1;
  status = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  status = bind(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);

  if (status == -1) {
    cerr << "Error: cannot bind socket" << endl;
    cerr << "  (" << hostname << "," << port << ")" << endl;
    return -1;
  } // if

  status = listen(socket_fd, 500);
  if (status == -1) {
    cerr << "Error: cannot listen on socket" << endl;
    cerr << "  (" << hostname << "," << port << ")" << endl;
    return -1;
  } // if

  cout << "Welcome to our http caching proxy " << endl;
  int ID = 0;
  //  skeleton_daemon();
  // syslog(LOG_NOTICE, "Daemon started.");
  try {
    while (1) {
      struct sockaddr_storage socket_addr;
      socklen_t socket_addr_len = sizeof(socket_addr);
      int client_connection_fd;
      try {
        client_connection_fd = accept(
            socket_fd, (struct sockaddr *)&socket_addr, &socket_addr_len);
        if (client_connection_fd == -1) {
          cerr << "Error: cannot accept connection on socket" << endl;
          return -1;
        }

      } catch (std::exception &e) {
        std::cerr << "Socket accept exception: " << e.what() << endl;
        return 0;
      }

      string socket_ip(
          inet_ntoa(((struct sockaddr_in *)&socket_addr)->sin_addr));

      std::thread t(proxy_work, ID, client_connection_fd, socket_ip);
      t.detach();
      ID++;
    }
    freeaddrinfo(host_info_list);
    close(socket_fd);
  }

  catch (std::exception &e) {
    cerr << "The proxy is closed by " << e.what() << endl;
    return -1;
  }
  // syslog(LOG_NOTICE, "Daemon terminated.");
  // closelog();
  return 0;
}
