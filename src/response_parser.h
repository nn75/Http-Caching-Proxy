#ifndef __RESPONSE_PARSER__H__
#define __RESPONSE_PARSER__H__
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>

using namespace std;
class response_parser {
private:
    string response_in;
    
    int chunked;
    int status;
    string status_line;
    string status_code;
    long int content_length;
    double age;
    string expire_info;
    string e_tag;
    string cache_control;
    string date;
    string last_modified;
    
    void check_status();
    void check_chunked();
    
public:
    response_parser(string res) {
        response_in = res;
        content_length = 0;
        chunked = 0;
        status = 0;
        status_line = "";
        expire_info = "";
        e_tag = "";
        date = "";
        last_modified = "";
        age = -1;
    }
    void parse();
    int get_status() { return status; }
    int get_chunked() { return chunked; }
    int get_content_length() { return content_length; }
    string get_status_line() { return status_line; }
    string get_expire_info() { return expire_info; }
    string get_e_tag() { return e_tag; }
    string get_cache_control() { return cache_control; }
    string get_date() { return date; }
    string get_last_modified() { return last_modified; }
    string get_status_code() { return status_code; }
    double get_age() { return age; }
    ~response_parser() {}
};
void response_parser::check_status() {
    
    if (response_in.find("200 OK") != string::npos)
        status = 1;
    else
        status = 0;
    status_line = response_in.substr(0, response_in.find("\r\n"));
    size_t numberbegin = status_line.find_first_of(" ") + 1;
    size_t numberend = status_line.find_first_of(" ", numberbegin);
    status_code = status_line.substr(numberbegin, numberend - numberbegin);
}
void response_parser::check_chunked() {
    if (response_in.find("chunked") != string::npos)
        chunked = 1;
    else
        chunked = 0;
}
void response_parser::parse() {
    check_status();
    check_chunked();
    size_t datebegin = response_in.find("Date:") + 6;
    if (response_in.find("Date:") != string ::npos) {
        size_t dateend = response_in.find("\r\n", datebegin);
        date = response_in.substr(datebegin, dateend - datebegin);
    } else {
        date = "";
    }
    size_t modifiedbegin = response_in.find("Last-Modified:") + 15;
    if (response_in.find("Last-Modified:") != string ::npos) {
        size_t modifiedend = response_in.find("\r\n", modifiedbegin);
        last_modified =
        response_in.substr(modifiedbegin, modifiedend - modifiedbegin);
    } else {
        last_modified = "";
    }
    size_t expiresbegin = response_in.find("Expires:") + 9;
    if (response_in.find("Expires:") != string ::npos) {
        size_t expiresend = response_in.find("\r\n", expiresbegin);
        expire_info = response_in.substr(expiresbegin, expiresend - expiresbegin);
    } else {
        expire_info = "";
    }
    size_t etagbegin = response_in.find("ETag:");
    if (etagbegin != string ::npos) {
        size_t etagend = response_in.find("\r\n", etagbegin);
        e_tag = response_in.substr(etagbegin, etagend - etagbegin);
    } else {
        e_tag = "";
    }
    size_t cachecontrolbegin = response_in.find("Cache-Control:");
    if (cachecontrolbegin != string ::npos) {
        size_t cachecontrolend = response_in.find("\r\n", cachecontrolbegin);
        cache_control = response_in.substr(cachecontrolbegin,
                                           cachecontrolend - cachecontrolbegin);
        
        size_t agebegin = cache_control.find("max-age=");
        if (agebegin != string::npos) {
            
            stringstream ss;
            agebegin += 8;
            string tmp = cache_control.substr(agebegin);
            ss << tmp;
            ss >> age;
        }
        
    } else {
        
        cache_control = "";
    }
    size_t contlenbegin = response_in.find("Content-Length:") + 16;
    if (response_in.find("Content-Length:") != string ::npos) {
        size_t contlenend = response_in.find("\r\n", contlenbegin);
        string str = response_in.substr(contlenbegin, contlenend - contlenbegin);
        stringstream ss;
        ss << str;
        ss >> content_length;
    }
}
#endif
