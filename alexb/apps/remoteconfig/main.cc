/*
 * Copyright (C) 2011, Alexander Boettcher <boettcher@tudos.org>
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

#include <nul/program.h>
#include <nul/timer.h> //clock
#include <nul/service_timer.h> //TimerService
#include <nul/service_log.h>
#include <nul/service_config.h>
#include <service/endian.h>
#include <service/cmdline.h>

#include <sigma0/sigma0.h> // Sigma0Base object
#include <sigma0/console.h>

#include "server.h"
#include "events.h"

extern "C" void nul_ip_input(void * data, unsigned size);
extern "C" bool nul_ip_init(void (*send_network)(char unsigned const * data, unsigned len), unsigned long long mac); 
extern "C" bool nul_ip_config(unsigned para, void * arg);

enum {
  IP_NUL_VERSION  = 0,
  IP_DHCP_START   = 1,
  IP_IPADDR_DUMP  = 2,
  IP_TIMEOUT_NEXT = 3,
  IP_UDP_OPEN     = 4,
  IP_TCP_OPEN     = 5,
  IP_SET_ADDR     = 6,
  IP_TCP_SEND     = 7
};

namespace ab {

class RemoteConfig : public NovaProgram, public ProgramConsole
{
  private:
    static Remcon * remcon;

  public:

    static void send_network(char unsigned const * data, unsigned len) {
      bool res;

      MessageNetwork net = MessageNetwork(data, len, 0);

      res = Sigma0Base::network(net);

      if (res) Logging::printf("%s - sending packet to network, len = %u, res= %u\n", 
                               (res == 0 ? "done   " : "failure"), len, res);
    }

    static
    void recv_call_back(void * in, size_t in_len, void * & out, size_t & out_len) {
      remcon->recv_call_back(in, in_len, out, out_len);    
    }

    unsigned  create_ec4pt(void * tls, phy_cpu_no cpunr, unsigned excbase, Utcb **utcb_out=0, unsigned long cap = ~0UL) {
      return NovaProgram::create_ec4pt(tls, cpunr, excbase, utcb_out, cap);
    }

    bool start_services(Utcb *utcb, Hip * hip, EventProducer * producer) {
      //create network service object
      ConfigProtocol *service_config = new ConfigProtocol(alloc_cap(ConfigProtocol::CAP_SERVER_PT + hip->cpu_desc_count()));
      unsigned cap_region = alloc_cap_region(1 << 14, 14);
      if (!cap_region) Logging::panic("failure - starting libvirt backend\n");
      remcon = new Remcon(reinterpret_cast<char const *>(_hip->get_mod(0)->aux), service_config, hip->cpu_desc_count(),
                          cap_region, 14, producer);

      //create event service object
      EventService * event = new EventService(remcon);
      if (!event) return false;
      return event->start_service(utcb, hip, this);
    }

    bool use_network(Utcb *utcb, Hip * hip, EventConsumer * sendconsumer,
                     Clock * _clock, KernelSemaphore &sem, TimerProtocol * timer_service)
    {
      bool res;
      unsigned long long arg = 0;

      if (!nul_ip_config(IP_NUL_VERSION, &arg) || arg != 0x2) return false;

      NetworkConsumer * netconsumer = new NetworkConsumer();
      if (!netconsumer) return false;
      res = Sigma0Base::request_network_attach(utcb, netconsumer, sem.sm());
      Logging::printf("%s - request network attach\n", (res == 0 ? "done   " : "failure"));
      if (res) return false;

      unsigned long long nmac;
      MessageNetwork msg_op(MessageNetwork::QUERY_MAC, 0);
      res = Sigma0Base::network(msg_op);
      nmac = msg_op.mac;
      if (res) {
        MessageHostOp msg_op1(MessageHostOp::OP_GET_MAC, 0UL);
        res = Sigma0Base::hostop(msg_op1);
        nmac = msg_op1.mac;
      }
      Logging::printf("%s - mac %02llx:%02llx:%02llx:%02llx:%02llx:%02llx\n",
                      (res == 0 ? "done   " : "failure"),
                      (nmac >> 40) & 0xFF, (nmac >> 32) & 0xFF,
                      (nmac >> 24) & 0xFF, (nmac >> 16) & 0xFF,
                      (nmac >> 8) & 0xFF, (nmac) & 0xFF);

      unsigned long long mac = Endian::hton64(nmac) >> 16;

      if (!nul_ip_config(IP_TIMEOUT_NEXT, &arg)) Logging::panic("failure - request for timeout\n");

      TimerProtocol::MessageTimer to(_clock->abstime(arg, 1));
      if (timer_service->timer(*utcb, to)) Logging::panic("failure - programming timer\n");
      if (!nul_ip_init(send_network, mac)) Logging::panic("failure - starting ip stack\n");

      // check for static ip - otherwise use dhcp
      char *cmdline = reinterpret_cast<char *>(hip->get_mod(0)->aux);
      char *args[16];
      unsigned entry, argv = Cmdline::parse(cmdline, args, sizeof(args)/sizeof(char *));
      unsigned long addr[4] = { 0, 0x00ffffffUL, 0, 0 }; //addr, netmask, gw
      bool static_ip = false;
      char * pos_s;
      for (unsigned i=1; i < argv && i < 16; i++) {
        if (!strncmp("ip="  , args[i],3)) { entry=0; pos_s = args[i] + 3; static_ip = true; goto parse; }
        if (!strncmp("mask=", args[i],5)) { entry=1; pos_s = args[i] + 5; goto parse; }
        if (!strncmp("gw="  , args[i],3)) { entry=2; pos_s = args[i] + 3; goto parse; }
        continue;

        parse:
          unsigned long num = 0;
          char * pos_e;
          for (unsigned j=0; *pos_s != 0 && j < 4; j++) {
            num = strtoul(pos_s, 0, 0);
            addr[entry] = num << 24 | (addr[entry] >> 8);
            pos_e = strstr(pos_s, ".");
            if (pos_e) pos_s = pos_e + 1;
            else break;
          }
          if (entry == 0 && pos_s && *pos_s != 0 && (pos_e = strstr(pos_s, "/"))) {
              pos_s = pos_e + 1;
              unsigned long num = strtoul(pos_s, 0, 0);
              if (num < 33) {
                addr[1] = ((1UL << (32 - num)) - 1);
                addr[1] = ~addr[1];
                addr[1] = ((addr[1] & 0xff) << 24) | ((addr[1] & 0x0000ff00UL) << 8) | ((addr[1] & 0x00ff0000UL) >> 8) | (addr[1] >>24);
              }
          }

//          Logging::printf("config  - %u %lu.%lu.%lu.%lu\n", entry, addr[entry] & 0xff,
//                         (addr[entry] >> 8) & 0xff, (addr[entry] >> 16) & 0xff, (addr[entry] >> 24) & 0xff);
      }
      if (static_ip)
        Logging::printf("%s - static ip=%lu.%lu.%lu.%lu mask=%lu.%lu.%lu.%lu gw=%lu.%lu.%lu.%lu\n",
                       nul_ip_config(IP_SET_ADDR, addr) ? "config " : "failure",
                       addr[0] & 0xff, (addr[0] >> 8) & 0xff, (addr[0] >> 16) & 0xff, (addr[0] >> 24) & 0xff,
                       addr[1] & 0xff, (addr[1] >> 8) & 0xff, (addr[1] >> 16) & 0xff, (addr[1] >> 24) & 0xff,
                       addr[2] & 0xff, (addr[2] >> 8) & 0xff, (addr[2] >> 16) & 0xff, (addr[2] >> 24) & 0xff);
      else
        if (!nul_ip_config(IP_DHCP_START, NULL)) Logging::panic("failure - starting dhcp service\n");

      struct {
        unsigned long port;
        void (*fn)(void * in_data, size_t in_len, void * &out_data, size_t & out_len);
      } conn = { 9999, recv_call_back };
      if (!nul_ip_config(IP_TCP_OPEN, &conn.port)) Logging::panic("failure - opening tcp port\n");

      conn = { 10000, 0 };
      if (!nul_ip_config(IP_TCP_OPEN, &conn.port)) Logging::panic("failure - opening tcp port\n");

      Logging::printf("done    - open tcp port %lu - %lu\n", conn.port - 1, conn.port);
      if (!static_ip)
        Logging::printf(".......   looking for an IP address via DHCP\n");

      while (1) {
        unsigned char *buf;
        unsigned tcount = 0;

        sem.downmulti();

        //check whether timer triggered
        if (!timer_service->triggered_timeouts(*utcb, tcount) && tcount) {
          unsigned long long timeout;

          nul_ip_config(IP_TIMEOUT_NEXT, &timeout);
          TimerProtocol::MessageTimer to(_clock->time() + timeout * hip->freq_tsc);
          if (timer_service->timer(*utcb,to)) Logging::printf("failure - programming timer\n");

          //dump ip addr if we got one
          if (nul_ip_config(IP_IPADDR_DUMP, NULL))
            Logging::printf("ready   - NOVA management daemon is up. Waiting for libvirt connection ... \n");
        }

        while (netconsumer->has_data()) {
          unsigned size = netconsumer->get_buffer(buf);
          nul_ip_input(buf, size);
          netconsumer->free_buffer();
        }

        while (sendconsumer->has_data()) {
          unsigned size = sendconsumer->get_buffer(buf);

          struct {
            unsigned long port;
            size_t count;
            void * data;
          } arg = { conn.port, size, buf };
          nul_ip_config(IP_TCP_SEND, &arg);
          sendconsumer->free_buffer();
        }
      }

      return !res;
    }

  void run(Utcb *utcb, Hip *hip)
  {

    init(hip);
    init_mem(hip);

    console_init("NOVA daemon", new Semaphore(alloc_cap(), true));
    _console_data.log = new LogProtocol(alloc_cap(LogProtocol::CAP_SERVER_PT + hip->cpu_desc_count()));

    Logging::printf("booting - NOVA daemon ...\n");

    Clock * _clock = new Clock(hip->freq_tsc);

    TimerProtocol * timer_service = new TimerProtocol(alloc_cap(TimerProtocol::CAP_SERVER_PT + hip->cpu_desc_count()));
    TimerProtocol::MessageTimer msg(_clock->abstime(0, 1000));
    bool res = timer_service->timer(*utcb, msg);

    Logging::printf("%s - request timer attach\n", (res == 0 ? "done   " : "failure"));
    if (res) Logging::panic("failure - attaching to timer service");

    KernelSemaphore sem = KernelSemaphore(timer_service->get_notify_sm());

    EventConsumer * send_consumer = new EventConsumer();
    EventProducer * send_producer = new EventProducer(send_consumer, sem.sm());

    if (!start_services(utcb, hip, send_producer)) Logging::panic("failure - starting event collector\n");
    if (!use_network(utcb, hip, send_consumer, _clock, sem, timer_service)) Logging::printf("failure - starting ip stack\n");

  }
};

} /* namespace */

Remcon * ab::RemoteConfig::remcon;

ASMFUNCS(ab::RemoteConfig, NovaProgram)
