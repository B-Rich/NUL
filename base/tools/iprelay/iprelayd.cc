/**
 * @file IP relay daemon
 *
 * This program serves as a proxy to the IP relay controlling the test
 * bed. Compared to the IP relay itself, it supports multiple
 * connections. If somebody else is connected to the IP relay, the
 * user is notified about this and can, with the help of novaboot,
 * wait for the IP relay to become free.
 *
 * This program supports two user priorities. A high priority user can
 * interrupt sessions of low priority users. The low priority is meant
 * for remote (PASSIVE) users, high priority is for local (TUD) users.
 *
 * Copyright (C) 2012, Michal Sojka <sojka@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NUL (NOVA user land).
 *
 * NUL is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * NUL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

using namespace std;

#define MAX_CLIENTS 3

// NVT codes (http://www.ietf.org/rfc/rfc854.txt)
#define  SE   "\xF0"
#define  AYT  "\xF6"
#define  SB   "\xFA"
#define  WILL "\xFB"
#define  WONT "\xFC"
#define  DO   "\xFD"
#define  DONT "\xFE"
#define  IAC  "\xFF"

const string are_you_there(IAC AYT);
const string reset_on (IAC SB "\x2C\x32\x25" IAC SE);
const string reset_off(IAC SB "\x2C\x32\x15" IAC SE);
const string power_on (IAC SB "\x2C\x32\x26" IAC SE);
const string power_off(IAC SB "\x2C\x32\x26" IAC SE);

const string reset_on_confirmation (IAC SB "\x2C\x97\xDF" IAC SE);
const string reset_off_confirmation(IAC SB "\x2C\x97\xFF" IAC SE);
const string power_on_confirmation (IAC SB "\x2C\x97\xBF" IAC SE);
const string power_off_confirmation(IAC SB "\x2C\x97\xFF" IAC SE);

enum {
  FD_LISTEN_LP,
  FD_LISTEN_HP,
  FD_IP_RELAY,
  FD_CLIENT,
  FD_COUNT = FD_CLIENT + MAX_CLIENTS
};

int msg(const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));
int msg(const char* fmt, ...)
{
  va_list ap;
  int ret;
  char *str;
  va_start(ap, fmt);
  ret = vasprintf(&str, fmt, ap);
  va_end(ap);
  unsigned l = strlen(str);
  if (l > 0 && str[l-1] == '\n')
    str[l-1] = 0;
  printf("%s\n", str);
  fflush(stdout);
  free(str);
  return ret;
}

class CommandStr : public string {
  bool  _can_match_later;
public:
  CommandStr() : string(), _can_match_later(false) {}
  CommandStr(char *str, size_t len) : string(str, len), _can_match_later(false) {}

  void reset_match() {
    _can_match_later = false;
  }

  bool match(const string &str)
  {
    if (*this == str) {
      return true;
    }

    if (*this == str.substr(0, length()))
      _can_match_later = true;

    return false;
  }

  bool can_match_later() const { return _can_match_later; }
};



class IpRelay {
  int              fd;
  struct addrinfo *ai;
  const char      *node;
  const char      *service;
  enum state {
    OFF, RST1, RST2, PWRON1, PWRON2, DATA, PWROFF1, PWROFF2
  } state;

  IpRelay(const IpRelay&);
  IpRelay& operator=(const IpRelay&);
public:
  IpRelay(const char *node, const char *service) : fd(-1), ai(0), node(node), service(service), state(OFF)
  {
    struct addrinfo hints;
    int ret;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_protocol = 0;          /* Any protocol */
    ret = getaddrinfo(node, service, &hints, &ai);
    if (ret != 0) {
      msg("getaddrinfo(%s, %s): %s\n", node, service, gai_strerror(ret));
      exit(1);
    }
  }

  ~IpRelay()
  {
    freeaddrinfo(ai);
  }

  int connect()
  {
    struct addrinfo *aip;

    for (aip = ai; aip != NULL; aip = aip->ai_next) {
      fd = socket(aip->ai_family, aip->ai_socktype,
                  aip->ai_protocol);
      if (fd == -1)
        continue;

      if (::connect(fd, aip->ai_addr, aip->ai_addrlen) != -1)
        break; /* Success */

      close(fd);
    }

    if (aip == NULL) {          /* No address succeeded */
      static bool first = true;
      if (first)
        msg("Could not connect to IP relay at %s:%s - will try later\n", node, service);
      first = false;
      fd = -1;
    } else
      msg("Connected to IP relay at %s:%s\n", node, service);
    return fd;
  }

  bool connected() const { return fd != -1; }

  void disconnect()
  {
    close(fd);
    fd = -1;
  }

  int send(const string &str)
  {
    if (!connected())
      msg("attempt to write to disconnected relay");
    return write(fd, str.data(), str.length());
  }

  void reset()
  {
    send(reset_on);
    state = RST1;
  }

  void poweroff()
  {
    send(power_on);
    state = PWROFF1;
  }

  int handle(pollfd &pfd, char *buf, size_t size)
  {
    if (pfd.revents & POLLRDHUP) {
      msg("IP relay disconnected\n");
      disconnect();
      memset(&pfd, 0, sizeof(pfd));
      return 0;
    }

    if (pfd.revents & POLLIN) {
      int ret = ::read(fd, buf, size);
      if (ret == -1)
        return ret;

      CommandStr reply(buf, size);
      switch (state) {
      case OFF:
        msg("data in OFF state");
      case DATA:
        return ret;

      case RST1:
        if (reply.match(reset_on_confirmation)) {
          usleep(100000);
          send(reset_off);
          state = RST2;
        }
        break;
      case RST2:
        if (reply.match(reset_off_confirmation)) {
          send(power_on);
          state = PWRON1;
        }
        break;
      case PWRON1:
        if (reply.match(power_on_confirmation)) {
          usleep(100000);
          send(power_off);
          state = PWRON2;
        }
        break;
      case PWRON2:
        if (reply.match(reset_off_confirmation))
          state = DATA;
        break;
      case PWROFF1:
        if (reply.match(power_on_confirmation)) {
          sleep(6);
          send(power_off);
          state = PWROFF2;
        }
        break;
      case PWROFF2:
        if (reply.match(reset_off_confirmation))
          state = OFF;
        break;
      }
      return 0;
    }
    return -1;
  }
};

class Client {
  Client             *next;
  int                 fd;
  struct sockaddr_in  addr;
  char                buf[4096];
  string              to_relay;
  CommandStr          command;

  Client(const Client&);        // disable copy constructor
  Client&         operator = (const Client&);
public:
  static Client  *active;
  static IpRelay *ip_relay;
  bool            high_prio;

  Client(int fd, struct sockaddr_in &addr, bool hp = false) :
    next(0), fd(fd), addr(addr), to_relay(), command(), high_prio(hp) {}

  string name()
  {
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

    if (getnameinfo(reinterpret_cast<sockaddr*>(&addr), sizeof(addr), hbuf, sizeof(hbuf), sbuf,
                    sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV) == 0)
      return string(hbuf)+string(":")+string(sbuf);
    else
      return NULL;
  }

  int msg(const char* fmt, ...) __attribute__ ((format (printf, 2, 3)))
  {
    va_list ap;
    int ret;
    char *str;
    va_start(ap, fmt);
    ret = vasprintf(&str, fmt, ap);
    va_end(ap);
    unsigned l = strlen(str);
    if (l > 0 && str[l-1] == '\n')
      str[l-1] = 0;
    ::msg("%s: %s\n", name().c_str(), str);
    free(str);
    return ret;
  }

  Client *add_to_queue()
  {
    Client *kicked = 0;
    if (!active)
      active = this;
    else {
      // Add ourselves to the queue
      Client *c;
      if (!high_prio) {
        for (c = active; c->next; c = c->next);
        c->next = this;
      } else {
        // High prio clients can interrupt low prio ones :)
        if (!active->high_prio) {
          active->bye_bye("More privileged user connected");
          kicked = active;
          next = active->next;
          active = this;
        } else {
          Client *last_hp = 0;
          for (c = active; c && c->high_prio; last_hp = c, c = c->next);
          next = last_hp->next;
          last_hp->next = this;
        }
      }
    }
    return kicked;
  }

  void print_queue()
  {
    Client *c;
    printf("queue:\n");
    for (c = active; c; c = c->next)
      printf("  %s next=%p\n", c->name().c_str(), c->next);
    printf("end\n");
  }

  void del_from_queue()
  {
    if (active == this)
      active = next;
    else {
      Client *c;
      for (c = active; c->next != this; c = c->next);
      c->next = next;
    }

    if (active)
      active->handle();
  }

  void bye_bye(const string &message)
  {
    char *bye_msg;

    asprintf(&bye_msg, "%s. Closing connecion.\n", message.c_str());
    msg("%s", bye_msg);
    send(bye_msg);
    free(bye_msg);
    close(fd);
  }

  bool is_active() const { return active == this; }

  unsigned clients_before() const
  {
    unsigned i;
    Client *c = active;
    for (i = 0; c != this; i++, c = c->next);
    return i;
  }

  bool interpret_as_command(char ch)
  {
    if (ch != IAC[0] && command.empty())
      return false;

    command += ch;
    command.reset_match();

    if (command.match(are_you_there)) {
      char buf[100];
      if (is_active())
        snprintf(buf, sizeof(buf), "<iprelayd: connected>\n");
      else {
        unsigned q = clients_before();
        snprintf(buf, sizeof(buf), "<iprelayd: not connected (%d client%s before you)>\n", q, q > 1 ? "s":"");
      }
      send(buf, strlen(buf));
    } else if (command.match(reset_on)) {
      msg("reseting");
      ip_relay->reset();
      send(reset_on_confirmation);
    } else if (command.match(reset_off)) {
      send(reset_off_confirmation);
    } else if (command.match(power_on)) {
      send(power_on_confirmation);
    } else if (command.match(power_off)) {
      send(power_off_confirmation);
    } else if (!command.can_match_later()) {
      to_relay += command;
      command.clear();
    }
    return true;
  }

  bool handle()
  {
    int ret;
    ret = read(fd, buf, sizeof(buf));

    if (ret < 0) {
      msg("read: %s", strerror(errno));
      return false;
    }

    msg("read len=%d\n", ret);

    for (int i = 0; i < ret; i++) {
      if (!interpret_as_command(buf[i]))
        to_relay += buf[i];
    }

    if (!to_relay.empty()) {
      if (active == this) {
        ip_relay->send(to_relay);
        to_relay.clear();
      } else {
        bye_bye("IP relay is used by somebody else");
        return false;
      }
    }
    return true;
  }

  int send(const char *str, size_t len)
  {
    return write(fd, str, len);
  }

  int send(const string &str)
  {
    return write(fd, str.data(), str.length());
  }
};

Client  *Client::active;
IpRelay *Client::ip_relay;

#define CHECK(cmd) ({ int ret = (cmd); if (ret == -1) { perror(#cmd); exit(1); }; ret; })

int main(int argc, char *argv[])
{
  struct sockaddr_in    my_addr;
  int                   yes = 1;
  static struct pollfd  pollfds[FD_COUNT];
  static Client        *clients[FD_COUNT];

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <IP_address> [<port>]\n", argv[0]);
    exit(1);
  }

  msg("Starting iprelayd");

  IpRelay ip_relay(argv[1], argc > 2 ? argv[2] : "23");
  Client::ip_relay = &ip_relay;

  int sfd;
  sfd = CHECK(socket(PF_INET, SOCK_STREAM, 0));
  CHECK(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));
  memset(&my_addr, 0, sizeof(my_addr));
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(2323);
  my_addr.sin_addr.s_addr = INADDR_ANY;
  CHECK(bind(sfd, reinterpret_cast<const sockaddr*>(&my_addr), sizeof(my_addr)));
  CHECK(listen(sfd, 10));
  pollfds[FD_LISTEN_LP] = { sfd, POLLIN };

  sfd = CHECK(socket(PF_INET, SOCK_STREAM, 0));
  CHECK(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));
  my_addr.sin_port = htons(2324);
  CHECK(bind(sfd, reinterpret_cast<const sockaddr*>(&my_addr), sizeof(my_addr)));
  CHECK(listen(sfd, 10));
  pollfds[FD_LISTEN_HP] = { sfd, POLLIN };

  while (1) {
    if (!ip_relay.connected()) {
      int fd = ip_relay.connect();
      if (ip_relay.connected())
        pollfds[FD_IP_RELAY] = { fd, POLLIN | POLLRDHUP };
    }

    CHECK(poll(pollfds, FD_COUNT, ip_relay.connected() ? -1 : 1000));

    if (pollfds[FD_LISTEN_LP].revents) {
      socklen_t          sin_size;
      struct sockaddr_in their_addr;
      unsigned           i;

      sin_size   = sizeof(their_addr);
      int new_fd = CHECK(accept(pollfds[FD_LISTEN_LP].fd, (struct sockaddr *)&their_addr, &sin_size));
      Client *c = new Client(new_fd, their_addr);
      c->msg("connected");
      for (i = FD_CLIENT; i < FD_COUNT && pollfds[i].events; i++);
      if (i < FD_COUNT) {
        pollfds[i] = { new_fd, POLLIN | POLLRDHUP };
        clients[i] = c;
        c->add_to_queue();
      } else {
        c->bye_bye("Too many connections");
        delete c;
      }
    }

    if (pollfds[FD_LISTEN_HP].revents) {
      socklen_t           sin_size;
      struct sockaddr_in  their_addr;
      unsigned            i;

      sin_size   = sizeof(their_addr);
      int new_fd = CHECK(accept(pollfds[FD_LISTEN_HP].fd, (struct sockaddr *)&their_addr, &sin_size));
      Client *c = new Client(new_fd, their_addr, true);
      c->msg("connected (high prio)");
      Client *kicked = c->add_to_queue();
      if (kicked) {
        for (i = FD_CLIENT; i < FD_COUNT && clients[i] != kicked; i++);
        assert(i < FD_COUNT);
        memset(&pollfds[i], 0, sizeof(pollfds[i]));
        clients[i] = 0;
        delete kicked;
      } else
        for (i = FD_CLIENT; i < FD_COUNT && pollfds[i].events; i++);
      if (i < FD_COUNT) {
        pollfds[i] = { new_fd, POLLIN | POLLRDHUP };
        clients[i] = c;
      } else {
        c->bye_bye("Too many connections");
        c->del_from_queue();
        delete c;
      }
    }

    if (pollfds[FD_IP_RELAY].revents) {
      unsigned revents = pollfds[FD_IP_RELAY].revents;
      msg("ip_relay handle %#x\n", revents);

      char buf[2000];
      int ret = ip_relay.handle(pollfds[FD_IP_RELAY], buf, sizeof(buf));

      if (ret == -1) {
        msg("iprelay error: %s", strerror(errno));
        exit(1);
      }

      if (Client::active)
        Client::active->send(buf, ret);
      write(1, buf, ret);     // Copy to stdout
    }

    for (unsigned i = FD_CLIENT; i < FD_COUNT; i++) {
      if (pollfds[i].revents) {
        Client *c = clients[i];
        c->msg("handle %#x", pollfds[i].revents);
        bool disconnected = false;
        if (pollfds[i].revents & POLLIN)
          disconnected = !c->handle();

        if (disconnected || pollfds[i].revents & POLLRDHUP) {
          c->msg("disconnected");
          c->del_from_queue();
          delete c;
          memset(&pollfds[i], 0, sizeof(pollfds[i]));
          clients[i] = 0;
        }
      }
    }
  }
}
