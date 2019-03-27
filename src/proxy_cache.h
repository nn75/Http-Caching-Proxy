#ifndef __PROXY_CACHE__H__
#define __PROXY_CACHE__H__
#define LOG "/var/log/erss/proxy.log"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace std;
mutex mylock;
class response_block {
public:
  vector<char> buffer;
  string status_line;
  string status_code;
  int header_len;
  int content_len;
  int total_len;
  int ID;
  bool revalidation;
  double age;
  string createtime;
  string lastmodified;
  string etag;
  response_block(vector<char> bf, string sline, string scode, int hlen,
                 int clen, int tlen, int id, bool rv, double a, string ct,
                 string lm, string et)
      : buffer(bf), status_line(sline), status_code(scode), header_len(hlen),
        content_len(clen), total_len(tlen), ID(id), revalidation(rv), age(a),
        createtime(ct), lastmodified(lm), etag(et) {}
  ~response_block() {}
};
class cache_block {
public:
  string key; // request line
  response_block *value;
  cache_block *prev;
  cache_block *next;
  string expiretime;
  cache_block(string k, response_block *v, string e)
      : key(k), value(v), prev(NULL), next(NULL), expiretime(e) {}
  ~cache_block() {}
};
class cache_list {
public:
  cache_block *head;
  cache_block *tail;
  int length;
  ofstream proxylog;
  cache_list() : head(NULL), tail(NULL), length(0) {}
  cache_list(const cache_list &in) {
    head = in.head;
    tail = in.tail;
    length = in.length;
  }
  void add_response_to_cache(string k, response_block *RSB, string expire_info);
  cache_block *search(string k, int ID);
  void update_reponse_to_head(cache_block *in_cache);
  void remove_response();
  void add_new_response(cache_block *new_r);
  ~cache_list() {}
};

cache_block *cache_list::search(string k, int ID) {
  //  cout << ID << " insearch" << endl;
  cache_block *curr = head;
  while (curr != NULL) {
    if (curr->key == k) {
      //  cout << ID << "found" << endl;
      return curr;
    } else {
      curr = curr->next;
    }
  }
  // cout << ID << "search done" << endl;
  return curr;
}

void cache_list::add_response_to_cache(string k, response_block *RSB,
                                       string expire_info) {
  mylock.lock();
  // cout << RSB->ID << "inlock" << endl;

  cache_block *in_cache = search(k, RSB->ID);
  if (in_cache != NULL) { ////Already in cache, but not the head
    free(in_cache->value);
    in_cache->value = RSB;
    in_cache->expiretime = expire_info;
    update_reponse_to_head(in_cache);
  } else {
    if (length >= 500) { // cache is full
      remove_response();
    }

    cache_block *new_response =
        new cache_block(k, RSB, expire_info); // Add parameters!!!
    add_new_response(new_response);
  }
  mylock.unlock();
  // cout << RSB->ID << "unlock" << endl;
  return;
}

void cache_list::update_reponse_to_head(cache_block *in_cache) {
  //  cout << "inupdate" << endl;
  /*  cache_block *tmp = head;
   cout << "before" << endl;
   while (tmp != NULL) {
   cout << tmp->key << "->" << endl;
   tmp = tmp->next;
   }
   */
  if (in_cache == head) {
    return;
  } else if (in_cache == tail) {
    tail = in_cache->prev;
    in_cache->prev->next = in_cache->next;
    in_cache->next = head;
    in_cache->prev = NULL;
    head->prev = in_cache;
    head = in_cache;
  } else {
    in_cache->prev->next = in_cache->next;
    in_cache->next->prev = in_cache->prev;
    in_cache->next = head;
    in_cache->prev = NULL;
    head->prev = in_cache;
    head = in_cache;
  }
  // cout << "after" << endl;
  /* tmp = head;
   while (tmp != NULL) {
   cout << tmp->key << "->" << endl;
   tmp = tmp->next;
   }*/
}

void cache_list::remove_response() {
  // cout << "remove" << endl;
  // cache_block *tmp = head;
  /*while (tmp != NULL) {
   cout << tmp->key << "->" << endl;
   tmp = tmp->next;
   }*/
  // cout << tail->key << endl;
  string evicted = tail->key;
  cache_block *tofree = tail;

  // cout << "tail prev" << tail->prev->key << endl;
  tail->prev->next = NULL;
  tail = tail->prev;
  // cout << "newtail" << tail->key << endl;

  tail->next = NULL;
  //  tmp = head;
  // cout << "after" << endl;
  /* while (tmp != NULL) {
   cout << tmp->key << "->" << endl;
   tmp = tmp->next;
   }*/
  free(tofree->value);
  free(tofree);
  length--;
  size_t firspace = evicted.find_first_of(" ");
  size_t secspace = evicted.find_first_of(" ", firspace + 1);
  int urllen = secspace - firspace - 1;
  string url = evicted.substr(firspace + 1, (urllen));
  proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
  proxylog << "(no-id): "
           << "evicted " << evicted << " from cache " << endl;
  proxylog.close();
}

void cache_list::add_new_response(cache_block *new_r) {
  // head->prev = new_r;
  new_r->prev = NULL;
  new_r->next = head;
  if (head != NULL) {
    head->prev = new_r;
  }

  head = new_r;
  if (tail == NULL) {
    tail = head;
  }
  length++;

  // cout << (new_r->value)->ID << " length " << length << endl;
  if (!new_r->expiretime.empty()) {
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << (new_r->value)->ID << ": cached, expires at "
             << new_r->expiretime << endl;
    proxylog.close();
  } else {
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << (new_r->value)->ID << ": cached " << endl;
    proxylog.close();
  }
  if ((new_r->value)->revalidation == true) {
    proxylog.open(LOG, std::ofstream::out | std::ofstream::app);
    proxylog << (new_r->value)->ID << ": cached, but requires re-validation "
             << endl;
    proxylog.close();
  }
}

#endif
