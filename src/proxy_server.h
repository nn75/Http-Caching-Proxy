#ifndef __PROXY_SERVER__H__
#define __PROXY_SERVER__H__
#define LOG "/var/log/erss/proxy.log"
#include "proxy_cache.h"
#include "request_parser.h"
#include "response_parser.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace std;

class proxy_server {
private:
  int ID;
  int remote_fd;
  int client_connection_fd;
  string socket_ip;
  string remote_hostname;
  string remote_port;
  string request_line;
  string status_line;
  string url;
  string request_in;
  long int content_length;
  ofstream proxylog;
  void get_request(cache_list &cache);
  string current_time();
  void method_get(cache_list &cache);
  void method_connect();
  void method_post(cache_list &cache) { get_response_from_remote(cache); };
  void pass_information();
  void get_response_from_remote(cache_list &cache);
  void get_response_from_validation(cache_block *origin, cache_list &cache);
  void chunked_response(vector<char> *buffer, int total);

public:
  proxy_server(int i, int cfd, string sip)
      : ID(i), client_connection_fd(cfd), socket_ip(sip), request_in(""),
        content_length(0) {}
  void work(cache_list &cache) { get_request(cache); }
  ~proxy_server() {}
};

string proxy_server::current_time() {
  time_t currentT = time(NULL);
  struct tm *tmp = gmtime(&currentT);
  if (tmp == NULL) {
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << ID << ": ERROR Gmtime error" << endl;
    proxylog.close();
    exit(EXIT_FAILURE);
  }
  char timestr[200];
  const char *fmt = "%a %b %d %T %Y";
  if (strftime(timestr, sizeof(timestr), fmt, tmp) == 0) {
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << ID << ": ERROR strftimr error" << endl;
    proxylog.close();
    exit(EXIT_FAILURE);
  }
  string ans(timestr);
  return ans;
}
void proxy_server::get_request(cache_list &cache) {
  try {
    request_in = "";
    string header = "";
    vector<char> *newbuffer;
    try {
      newbuffer = new vector<char>(65535);
    } catch (std::bad_alloc &ba) {
      std::cerr << "Bad alloc " << ba.what() << endl;
      return;
    }
    char *p = newbuffer->data();
    //  char buffer[20000];
    // memset(buffer, '\0', 20000);
    string timestr = current_time();
    long int total = 0;
    long int size = 65535;
    long int headerlen = 0;
    int len = recv(client_connection_fd, p, size, 0);
    total += len;
    while (1) {

      p = newbuffer->data();
      p += total;

      string tmp(newbuffer->begin(), newbuffer->end());

      if (total != 0 && total == (content_length + headerlen))
        break;
      size_t headerend = tmp.find("\r\n\r\n");
      if (header == "" && headerend != string::npos) {
        headerend = tmp.find("\r\n\r\n");
        headerend += 4;
        header = tmp.substr(0, headerend);
        headerlen = header.size();
        size_t content_l_pos = header.find("Content-Length:");
        if (content_l_pos == string::npos) {
          break;
        } else if (content_length == 0) {
          content_l_pos += 16;
          size_t end = header.find("\r\n", content_l_pos);
          stringstream ss;
          string content_length_str =
              header.substr(content_l_pos, end - content_l_pos);
          ss << content_length_str;
          ss >> content_length;
          // size = content_length-total;
        }
      }
      if (content_length != 0) {
        size = content_length + headerlen - total;
      }
      len = recv(client_connection_fd, p, size, 0);
      total += len;
      // cout << content_length << endl;
    }
    request_in = string(newbuffer->begin(), newbuffer->end());
    free(newbuffer);
    if (header.empty()) {
      const char *badreqres = "HTTP/1.1 400 Bad Request";
      send(client_connection_fd, badreqres, sizeof(badreqres), 0);
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR No request header" << endl;
      proxylog << ID << ": Responding HTTP/1.1 400 Bad Request" << endl;
      proxylog.close();

      close(client_connection_fd);
      return;
    }
    //    cout << request_in << endl;
    request_parser RP(header);
    RP.parser();
    request_line = RP.print_request_line();
    url = RP.get_url();
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << ID << ": \"" << request_line << "\" from " << socket_ip << " @ "
             << timestr << endl;
    proxylog.close();
    remote_port = RP.get_remote_port();
    remote_hostname = RP.get_remote_hostname();

    if (RP.get_method() == "GET") {
      method_get(cache);
    } else if (RP.get_method() == "CONNECT") {
      method_connect();
    } else if (RP.get_method() == "POST") {
      //    cout << request_in << endl;
      if (content_length == 0) {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": ERROR No content length in POST request" << endl;
        proxylog << ID << ": Responding HTTP/1.1 411 Length Required" << endl;
        proxylog.close();
        close(client_connection_fd);

        return;
      }

      method_post(cache);
    } else {
      const char *badreqres = "HTTP/1.1 400 Bad Request";
      send(client_connection_fd, badreqres, sizeof(badreqres), 0);
    }
    close(client_connection_fd);
  } catch (std::exception &e) {
    cerr << "Get request failure " << e.what() << endl;
    close(client_connection_fd);
    return;
  }
  return;
}
void proxy_server::method_get(cache_list &cache) {

  cache_block *find = cache.search(request_line, ID);
  if (find == NULL) {
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << ID << ": not in cache" << endl;
    proxylog.close();
    get_response_from_remote(cache);
    return;
  } else {
    string exp = find->expiretime;
    double maxage = (find->value)->age;
    string createtime = (find->value)->createtime;
    if ((find->value)->revalidation == true) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": in cache, requires validation" << endl;
      proxylog.close();
      get_response_from_validation(find, cache);
      return;
    }
    time_t currentT = time(NULL);
    struct tm *cur = gmtime(&currentT);
    if (maxage != -1) {
      struct tm dat;
      strptime(createtime.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &dat);
      time_t createT = mktime(&dat);
      time_t expireT = createT + maxage;
      struct tm *expi = gmtime(&expireT);
      if (expireT < currentT) {

        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": in cache, but expired at " << asctime(expi)
                 << endl;
        proxylog.close();
        get_response_from_validation(find, cache);
        return;
      } else {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": in cache, valid" << endl;
        proxylog.close();

        const char *sp = ((find->value)->buffer).data();
        send(client_connection_fd, sp, ((find->value)->total_len), 0);

        return;
      }

    } else if (exp != "") {
      struct tm expi2;
      strptime(exp.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &expi2);
      time_t expire2T = mktime(&expi2);
      if (expire2T < currentT) {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": in cache, but expired at " << exp << endl;
        proxylog.close();
        get_response_from_validation(find, cache);
        return;
      } else {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": in cache, valid" << endl;
        proxylog.close();
        const char *sp = ((find->value)->buffer).data();
        send(client_connection_fd, sp, ((find->value)->total_len), 0);
      }

    }

    else {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": in cache, valid" << endl;
      proxylog.close();

      const char *sp = ((find->value)->buffer).data();
      send(client_connection_fd, sp, ((find->value)->total_len), 0);
    }
  }
}

void proxy_server::method_connect() {
  int status;
  try {
    remote_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (remote_fd == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR Create socket failure" << endl;
      proxylog.close();
      exit(EXIT_FAILURE);
    }
    struct hostent *remoteinfo = gethostbyname(remote_hostname.c_str());
    if (remoteinfo == NULL) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR Find remote server hostname failure" << endl;
      proxylog.close();
      exit(EXIT_FAILURE);
    }
    struct sockaddr_in remote_in;
    remote_in.sin_family = AF_INET;
    int port;
    if (remote_port == "") {
      remote_port = "443";
    }
    stringstream ss;
    ss << remote_port;
    ss >> port;
    remote_in.sin_port = htons(port);
    memcpy(&remote_in.sin_addr, remoteinfo->h_addr_list[0],
           remoteinfo->h_length);

    status =
        connect(remote_fd, (struct sockaddr *)&remote_in, sizeof(remote_in));
    if (status == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR connect to remote server failure" << endl;
      proxylog.close();
      return;
    }

    const char *ack = "HTTP/1.1 200 OK\r\n\r\n";
    status = send(client_connection_fd, ack, strlen(ack), 0);
    if (status == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR Send ack to client failure" << endl;
      proxylog.close();
      return;
    }
    pass_information();
  }

  catch (std::exception &e) {
    cerr << "Connect to remote server failure " << e.what() << endl;
    close(remote_fd);
    return;
  }
  return;
}

void proxy_server::pass_information() {
  try {
    int status;
    char buffer[65535];
    memset(buffer, 0, strlen(buffer));
    fd_set readfds;
    int max_fd = -1;
    while (1) {
      max_fd =
          (remote_fd > client_connection_fd) ? remote_fd : client_connection_fd;
      FD_ZERO(&readfds);
      FD_SET(remote_fd, &readfds);
      FD_SET(client_connection_fd, &readfds);
      status = select(max_fd + 1, &readfds, NULL, NULL, NULL);
      if (status <= 0) {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": ERROR select()" << endl;
        proxylog.close();
        break;
      }
      memset(buffer, 0, strlen(buffer));
      if (FD_ISSET(remote_fd, &readfds)) {
        status = recv(remote_fd, buffer, sizeof(buffer), 0);
        if (status < 0) {
          proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
          proxylog << ID << ": ERROR Receive from remote failure" << endl;
          proxylog.close();
          break;
        } else if (status == 0) {
          close(remote_fd);
          proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
          proxylog << ID << ": Tunnel closed" << endl;
          proxylog.close();
          return;
        } else {
          status = send(client_connection_fd, buffer, status, 0);
          if (status == -1) {
            proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
            proxylog << ID << ": ERROR Send to browser failure" << endl;
            proxylog.close();
            break;
          }
        }
      } else if (FD_ISSET(client_connection_fd, &readfds)) {

        status = recv(client_connection_fd, buffer, sizeof(buffer), 0);
        if (status < 0) {
          proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
          proxylog << ID << ": ERROR Receive from browser failure" << endl;
          proxylog.close();
          break;
        } else if (status == 0) {
          close(remote_fd);
          proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
          proxylog << ID << ": Tunnel closed" << endl;
          proxylog.close();
          return;
        } else {
          status = send(remote_fd, buffer, status, 0);
          if (status == -1) {
            proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
            proxylog << ID << ": ERROR Send to remote server failure" << endl;
            proxylog.close();
            break;
          }
        }
      }
    } // while loop
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << ID << ": Tunnel closed" << endl;
    proxylog.close();
  } catch (std::exception &e) {
    cerr << "Pass information failure  " << e.what() << endl;
    close(remote_fd);
    return;
  }

  close(remote_fd);
  return;
}
void proxy_server::get_response_from_remote(cache_list &cache) {
  try {
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << ID << ": Requesting \"" << request_line << "\" from " << url
             << endl;
    proxylog.close();
    int status;
    remote_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (remote_fd == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR Create socket failure" << endl;
      proxylog.close();
      exit(EXIT_FAILURE);
    }
    struct hostent *remoteinfo = gethostbyname(remote_hostname.c_str());
    if (remoteinfo == NULL) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR Find remote server hostname failure" << endl;
      proxylog.close();
      close(remote_fd);
      return;
    }
    struct sockaddr_in remote_in;
    remote_in.sin_family = AF_INET;
    int port;
    if (remote_port == "") {
      remote_port = "80";
    }
    stringstream ss;
    ss << remote_port;
    ss >> port;
    remote_in.sin_port = htons(port);
    memcpy(&remote_in.sin_addr, remoteinfo->h_addr_list[0],
           remoteinfo->h_length);

    status =
        connect(remote_fd, (struct sockaddr *)&remote_in, sizeof(remote_in));
    if (status == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR connect to remote server failure" << endl;
      proxylog.close();
      close(remote_fd);
      return;
    }
    const char *request_tosend = request_in.c_str();
    status = send(remote_fd, request_tosend, strlen(request_tosend), 0);
    if (status == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR send request to remote server failure" << endl;
      proxylog.close();
      close(remote_fd);
      return;
    }
    vector<char> *newbuffer;
    try {
      newbuffer = new vector<char>(2000);
    } catch (std::bad_alloc &ba) {
      std::cerr << "Bad alloc " << ba.what() << endl;
      close(remote_fd);
      return;
    }

    long int total = 0;
    long int headerlen = 0;
    long int recvlen;
    long int size = 2000;
    long int response_content_length = -1;
    string response_header;
    char *p = newbuffer->data();
    recvlen = recv(remote_fd, p, size, 0);
    total += recvlen;
    string tmp(newbuffer->begin(), newbuffer->end());
    size_t headerend = tmp.find("\r\n\r\n");
    if (headerend != string::npos) {
      response_header = tmp.substr(0, headerend + 4);
      headerlen = response_header.size();

    }

    else {
      const char *badreqres = "HTTP/1.1 502 Bad Gateway";
      send(client_connection_fd, badreqres, sizeof(badreqres), 0);
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR No valid response header" << endl;
      proxylog << ID << ": Responding HTTP/1.1 502 Bad Gateway" << endl;
      proxylog.close();
      free(newbuffer);
      close(remote_fd);
      return;
    }
    response_parser RSP(response_header);
    RSP.parse();
    status_line = RSP.get_status_line();
    if (RSP.get_status() != 1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      if (status_line.find("404") != string::npos) {
        proxylog << ID << ": Responding HTTP/1.1 404 Not Found" << endl;
      }

      proxylog << ID << ": ERROR Not HTTP 200 OK" << endl;
      proxylog.close();
    }

    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << ID << ": Received \"" << status_line << "\" from "
             << remote_hostname << endl;
    proxylog.close();
    if (RSP.get_chunked() == 1) {
      chunked_response(newbuffer, total);
      free(newbuffer);
      return;
    }
    response_content_length = RSP.get_content_length();
    if (response_content_length + headerlen <= 2000 && total <= 2000) {
      if (RSP.get_cache_control() != "") {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": NOTE " << RSP.get_cache_control() << endl;
        proxylog.close();
      }
      if (RSP.get_e_tag() != "") {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": NOTE " << RSP.get_e_tag() << endl;
        proxylog.close();
      }
      if ((RSP.get_cache_control()).find("no-cache") == string::npos &&
          (RSP.get_cache_control()).find("no-store") == string::npos &&
          RSP.get_status() == 1) {
        if ((RSP.get_cache_control()).find("must-revalidate") == string::npos) {

          response_block *RSB = new response_block(
              *newbuffer, status_line, RSP.get_status_code(), headerlen,
              response_content_length, total, ID, false, RSP.get_age(),
              RSP.get_date(), RSP.get_last_modified(), RSP.get_e_tag());

          cache.add_response_to_cache(request_line, RSB, RSP.get_expire_info());

        } else {

          response_block *RSB = new response_block(
              *newbuffer, status_line, RSP.get_status_code(), headerlen,
              response_content_length, total, ID, true, RSP.get_age(),
              RSP.get_date(), RSP.get_last_modified(), RSP.get_e_tag());
          cache.add_response_to_cache(request_line, RSB, RSP.get_expire_info());
        }

      } else {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": not cacheable because the server responsed  "
                 << RSP.get_status_line() << " " << RSP.get_cache_control()
                 << endl;
        proxylog.close();
      }

      const char *sp = newbuffer->data();
      send(client_connection_fd, sp, total, 0);
      free(newbuffer);
      close(remote_fd);
      return;
    }
    //  cout << "content" << response_content_length << endl;
    // cout << "headerlen" << headerlen << endl;
    try {
      newbuffer->resize(response_content_length + headerlen);
    } catch (std::bad_alloc &ba) {
      std::cerr << "Bad alloc " << ba.what() << endl;
      close(remote_fd);
      return;
    }
    while (1) {
      p = newbuffer->data();
      p = p + total;
      size = response_content_length + headerlen - total;
      recvlen = recv(remote_fd, p, size, 0);
      total += recvlen;
      // cout << total << endl;
      if (total == response_content_length + headerlen)
        break;
    }
    //    cout << RSP.get_cache_control() << endl;
    if (RSP.get_cache_control() != "") {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": NOTE " << RSP.get_cache_control() << endl;
      proxylog.close();
    }
    if (RSP.get_e_tag() != "") {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": NOTE " << RSP.get_e_tag() << endl;
      proxylog.close();
    }
    if ((RSP.get_cache_control()).find("no-cache") == string::npos &&
        (RSP.get_cache_control()).find("no-store") == string::npos &&
        RSP.get_status() == 1) {
      if ((RSP.get_cache_control()).find("must-revalidate") == string::npos) {

        response_block *RSB = new response_block(
            *newbuffer, status_line, RSP.get_status_code(), headerlen,
            response_content_length, total, ID, false, RSP.get_age(),
            RSP.get_date(), RSP.get_last_modified(), RSP.get_e_tag());
        // cout << "1" << RSP.get_expire_info() << endl;
        cache.add_response_to_cache(request_line, RSB, RSP.get_expire_info());

      } else {

        response_block *RSB = new response_block(
            *newbuffer, status_line, RSP.get_status_code(), headerlen,
            response_content_length, total, ID, true, RSP.get_age(),
            RSP.get_date(), RSP.get_last_modified(), RSP.get_e_tag());
        // cout << "2" << RSP.get_expire_info() << endl;
        cache.add_response_to_cache(request_line, RSB, RSP.get_expire_info());
      }

    } else {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": not cacheable because the server responsed  "
               << RSP.get_status_line() << " " << RSP.get_cache_control()
               << endl;
      proxylog.close();
    }
    const char *sp = newbuffer->data();
    send(client_connection_fd, sp, total, 0);
    free(newbuffer);
    close(remote_fd);
  } catch (std::exception &e) {
    cerr << "Receive response remote server failure " << e.what() << endl;
    close(remote_fd);
    return;
  }
  return;
}
void proxy_server::chunked_response(vector<char> *buffer, int total) {
  try {
    send(client_connection_fd, buffer->data(), total, 0);
    int len;
    int status;
    // size_t find_last_chunk;
    vector<char> single_chunk(2000);
    while (1) {
      single_chunk.clear();
      len = recv(remote_fd, single_chunk.data(), 2000, 0);
      if (len < 0) {

        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": ERROR Single chunk receive error" << endl;
        proxylog.close();
        exit(EXIT_FAILURE);
      } else if (len == 0) {
        close(remote_fd);
        return;
      }
      status = send(client_connection_fd, single_chunk.data(), len, 0);
      if (status < 0) {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": ERROR Single chunk send to client failed" << endl;
        proxylog.close();
        exit(EXIT_FAILURE);
      }
    }
  }

  catch (std::exception &e) {
    cerr << "Chunk failure " << e.what() << endl;
    close(remote_fd);
    return;
  }
}
void proxy_server::get_response_from_validation(cache_block *origin,
                                                cache_list &cache) {
  try {
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << ID << ": Requesting \"" << request_line << "\" from " << url
             << endl;
    proxylog.close();
    int status;
    remote_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (remote_fd == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR Create socket failure" << endl;
      proxylog.close();
      exit(EXIT_FAILURE);
    }
    struct hostent *remoteinfo = gethostbyname(remote_hostname.c_str());
    if (remoteinfo == NULL) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR Find remote server hostname failure" << endl;
      proxylog.close();
      exit(EXIT_FAILURE);
    }
    struct sockaddr_in remote_in;
    remote_in.sin_family = AF_INET;
    int port;
    if (remote_port == "") {
      remote_port = "80";
    }
    stringstream ss;
    ss << remote_port;
    ss >> port;
    remote_in.sin_port = htons(port);
    memcpy(&remote_in.sin_addr, remoteinfo->h_addr_list[0],
           remoteinfo->h_length);

    status =
        connect(remote_fd, (struct sockaddr *)&remote_in, sizeof(remote_in));
    if (status == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR connect to remote server failure" << endl;
      proxylog.close();
      close(remote_fd);
      return;
    }
    vector<char> responsetosend(request_in.begin(), request_in.end() - 4);

    string tmp0 = origin->value->etag + "\r\n";
    vector<char> etagtosend(tmp0.begin(), tmp0.end());
    string tmp1 = "If-Modified-Since: ";
    string tmp2 = "\r\n\r\n\r\n";

    string tmp3 = tmp1 + origin->value->lastmodified + tmp2;

    vector<char> lastmoditosend(tmp3.begin(), tmp3.end());
    // a.insert(a.end(), b.begin(), b.end());
    responsetosend.insert(responsetosend.end(), etagtosend.begin(),
                          etagtosend.end());
    responsetosend.insert(responsetosend.end(), lastmoditosend.begin(),
                          lastmoditosend.end());
    char *response_tosend = responsetosend.data();
    //  cout << request_tosend << endl;
    status = send(remote_fd, response_tosend, strlen(response_tosend), 0);
    if (status == -1) {
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR send request to remote server failure" << endl;
      proxylog.close();
      close(remote_fd);
      return;
    }
    vector<char> *newbuffer;
    try {
      newbuffer = new vector<char>(2000);
    } catch (std::bad_alloc &ba) {
      std::cerr << "Bad alloc " << ba.what() << endl;
      close(remote_fd);
      return;
    }

    long int total = 0;
    long int headerlen = 0;
    long int recvlen;
    long int size = 2000;
    long int response_content_length = -1;
    string response_header;
    char *p = newbuffer->data();
    recvlen = recv(remote_fd, p, size, 0);
    total += recvlen;
    string tmp(newbuffer->begin(), newbuffer->end());
    size_t headerend = tmp.find("\r\n\r\n");
    if (headerend != string::npos) {
      response_header = tmp.substr(0, headerend + 4);
      headerlen = response_header.size();

    } else {
      const char *badreqres = "HTTP/1.1 502 Bad Gateway";
      send(client_connection_fd, badreqres, sizeof(badreqres), 0);
      proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
      proxylog << ID << ": ERROR No valid response header" << endl;
      proxylog << ID << ": Responding HTTP/1.1 502 Bad Gateway" << endl;
      proxylog.close();
      free(newbuffer);
      close(remote_fd);
      return;
    }

    response_parser RSP(response_header);
    RSP.parse();
    status_line = RSP.get_status_line();
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    if (status_line.find("404") != string::npos) {
      proxylog << ID << ": Responding HTTP/1.1 404 Not Found" << endl;
    }

    proxylog << ID << ": Received \"" << status_line << "\" from "
             << remote_hostname << endl;
    proxylog.close();

    if (RSP.get_status_line().find("304") != string::npos) {
      const char *sp = origin->value->buffer.data();
      send(client_connection_fd, sp, origin->value->total_len, 0);
      free(newbuffer);
      close(remote_fd);
      return;
    } else if (RSP.get_status_line().find("200") != string::npos) {
      if (RSP.get_chunked() == 1) {
        chunked_response(newbuffer, total);
        free(newbuffer);
        return;
      }
      response_content_length = RSP.get_content_length();
      if (response_content_length + headerlen <= 2000 && total <= 2000) {
        if (RSP.get_cache_control() != "") {
          proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
          proxylog << ID << ": NOTE " << RSP.get_cache_control() << endl;
          proxylog.close();
        }
        if (RSP.get_e_tag() != "") {
          proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
          proxylog << ID << ": NOTE " << RSP.get_e_tag() << endl;
          proxylog.close();
        }
        if ((RSP.get_cache_control()).find("no-cache") == string::npos &&
            (RSP.get_cache_control()).find("no-store") == string::npos &&
            RSP.get_status() == 1) {
          if ((RSP.get_cache_control()).find("must-revalidate") ==
              string::npos) {

            response_block *RSB = new response_block(
                *newbuffer, status_line, RSP.get_status_code(), headerlen,
                response_content_length, total, ID, false, RSP.get_age(),
                RSP.get_date(), RSP.get_last_modified(), RSP.get_e_tag());

            cache.add_response_to_cache(request_line, RSB,
                                        RSP.get_expire_info());

          } else {

            response_block *RSB = new response_block(
                *newbuffer, status_line, RSP.get_status_code(), headerlen,
                response_content_length, total, ID, true, RSP.get_age(),
                RSP.get_date(), RSP.get_last_modified(), RSP.get_e_tag());

            cache.add_response_to_cache(request_line, RSB,
                                        RSP.get_expire_info());
          }

        } else {
          proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
          proxylog << ID << ": not cacheable because the server responsed  "
                   << RSP.get_status_line() << " " << RSP.get_cache_control()
                   << endl;
          proxylog.close();
        }

        const char *sp = newbuffer->data();
        send(client_connection_fd, sp, total, 0);
        free(newbuffer);
        close(remote_fd);
        return;
      }
      //  cout << "content" << response_content_length << endl;
      // cout << "headerlen" << headerlen << endl;
      try {
        newbuffer->resize(response_content_length + headerlen);
      } catch (std::bad_alloc &ba) {
        std::cerr << "Bad alloc " << ba.what() << endl;
        close(remote_fd);
        return;
      }
      while (1) {
        p = newbuffer->data();
        p = p + total;
        size = response_content_length + headerlen - total;
        recvlen = recv(remote_fd, p, size, 0);
        total += recvlen;
        // cout << total << endl;
        if (total == response_content_length + headerlen)
          break;
      }
      if (RSP.get_cache_control() != "") {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": NOTE " << RSP.get_cache_control() << endl;
        proxylog.close();
      }
      if (RSP.get_e_tag() != "") {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": NOTE " << RSP.get_e_tag() << endl;
        proxylog.close();
      }
      if ((RSP.get_cache_control()).find("no-cache") == string::npos &&
          (RSP.get_cache_control()).find("no-store") == string::npos) {
        if ((RSP.get_cache_control()).find("must-revalidate") == string::npos) {

          response_block *RSB = new response_block(
              *newbuffer, status_line, RSP.get_status_code(), headerlen,
              response_content_length, total, ID, false, RSP.get_age(),
              RSP.get_date(), RSP.get_last_modified(), RSP.get_e_tag());
          // cout << "1" << RSP.get_expire_info() << endl;
          cache.add_response_to_cache(request_line, RSB, RSP.get_expire_info());

        } else {

          response_block *RSB = new response_block(
              *newbuffer, status_line, RSP.get_status_code(), headerlen,
              response_content_length, total, ID, true, RSP.get_age(),
              RSP.get_date(), RSP.get_last_modified(), RSP.get_e_tag());
          // cout << "2" << RSP.get_expire_info() << endl;
          cache.add_response_to_cache(request_line, RSB, RSP.get_expire_info());
        }

      } else {
        proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
        proxylog << ID << ": not cacheable because " << RSP.get_cache_control()
                 << endl;
        proxylog.close();
      }
      const char *sp = newbuffer->data();
      send(client_connection_fd, sp, total, 0);
      free(newbuffer);
      close(remote_fd);
      return;
    }
  } catch (std::exception &e) {
    cerr << "Receive to response from server validation failure " << e.what()
         << endl;
    close(remote_fd);
    return;
  }
}
#endif
