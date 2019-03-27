#ifndef __REQUEST_PARSER__H__
#define __REQUEST_PARSER__H__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace std;
class request_parser {
private:
    string header;
    string request_line;
    string method;
    string url;
    string remote_hostname;
    string remote_port;
    string get_request_line();
    
public:
    void parser();
    request_parser(string request_in)
    : header(request_in), remote_hostname(""), remote_port("") {}
    string get_url() { return url; }
    string print_request_line() { return request_line; }
    string get_method() { return method; }
    string get_remote_hostname() { return remote_hostname; }
    string get_remote_port() { return remote_port; }
    ~request_parser() {}
};
string request_parser::get_request_line() {
    size_t firstend = header.find("\r\n");
    string firstline = header.substr(0, firstend);
    return firstline;
}
void request_parser::parser() {
    request_line = get_request_line();
    size_t firspace = request_line.find_first_of(" ");
    method = request_line.substr(0, firspace);
    size_t secspace = request_line.find_first_of(" ", firspace + 1);
    int urllen = secspace - firspace - 1;
    url = request_line.substr(firspace + 1, (urllen));
    size_t hostpos = header.find("Host: ");
    hostpos += 6;
    size_t hostend = header.find("\r\n", hostpos);
    int hostinfolen = hostend - hostpos;
    string hostinfo = header.substr(hostpos, hostinfolen);
    remote_hostname = hostinfo;
    if (hostinfo.find_first_of(":") != string::npos) {
        remote_hostname = hostinfo.substr(0, hostinfo.find_first_of(":"));
        remote_port = hostinfo.substr(hostinfo.find_first_of(":") + 1).c_str();
    }
}
#endif
