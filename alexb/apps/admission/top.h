/*
 * Copyright (C) 2011, Alexander Boettcher <boettcher@tudos.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NUL (NOVA user land).
 *
 * NUL is free software: you can redistribute it and/or
 * This is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * NUL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

  enum {
    VALUEWIDTH = 2U,
    WIDTH  = 80U,
    HEIGHT = 25U,
  };

  void get_idle(Hip * hip) {
    cursor_pos = 0;

    unsigned cpu_from = 0;
    unsigned cpu_to   = 5U * hip->cpu_count() >= (WIDTH - 32U) ? (WIDTH - 32U) / 5U : hip->cpu_count();

    for (unsigned cpu = cpu_from; cpu < cpu_to; cpu++) {
      timevalue time_con = 0;
      ClientData * data = &idle_scs;
      for (unsigned i=0; i < sizeof(data->scs) / sizeof(data->scs[0]); i++) {
        if (!data->scs[i].idx || data->scs[i].cpu != cpu) continue;
        time_con += data->scs[i].m_last1 - data->scs[i].m_last2;
      }

      timevalue rest;
      splitfloat(time_con, rest, cpu);
      if (time_con >= 100)
        Vprintf::printf(&_putc, _vga_console + VALUEWIDTH * WIDTH, " 100");
      else
        Vprintf::printf(&_putc, _vga_console + VALUEWIDTH * WIDTH, "%2llu.%1llu", time_con, rest);

      cursor_pos -= 1;
    }
    memset(_vga_console + VALUEWIDTH * WIDTH + cursor_pos * VALUEWIDTH, 0, 1);
    cursor_pos = 0;
  }

  unsigned get_usage(Utcb & utcb, ClientData * data) {
    unsigned i, cpu;

    for (cpu=0; cpu < 32 ; cpu++) { //XXX CPU count
      timevalue time_con = 0;
      bool avail = false;

      for (i=0; i < sizeof(data->scs) / sizeof(data->scs[0]); i++) {
        if (!data->scs[i].idx || data->scs[i].cpu != cpu) continue;
        time_con += data->scs[i].m_last1 - data->scs[i].m_last2;
        avail = true;
      }
      if (!avail) continue;

      timevalue rest;
      splitfloat(time_con, rest, cpu);

      unsigned _util = time_con;
      unsigned _rest = rest;
      utcb << cpu << _util << _rest;
    }

    utcb << ~0UL;
    return ENONE;
  }

  unsigned measure(ClientData volatile * data, unsigned cpu)
  {
    unsigned i, res = ERESOURCE;

    for (i=0; i < sizeof(data->scs) / sizeof(data->scs[0]); i++) {
      if (!data->scs[i].idx || data->scs[i].cpu != cpu) continue;
      res = ENONE;

      timevalue computetime;
      unsigned res = nova_ctl_sc(data->scs[i].idx, computetime);
      if (res != NOVA_ESUCCESS) return EPERM;

      timevalue diff        = computetime - data->scs[i].m_last1; //XXX handle wraparounds ...
      global_sum[cpu]      += diff;
      global_sum[cpu]      -= data->scs[i].m_last1 - data->scs[i].m_last2;
      global_prio[cpu][data->scs[i].prio] += diff;
      global_prio[cpu][data->scs[i].prio] -= data->scs[i].m_last1 - data->scs[i].m_last2;
      data->scs[i].m_last2  = data->scs[i].m_last1;
      data->scs[i].m_last1  = computetime;
    }
    return res;
  }

  void top_dump_prio(Hip * hip) {
    unsigned row = 1;
    unsigned cpu_from = 0;
    unsigned cpu_to   = 5U * hip->cpu_count() >= (WIDTH - 5) ? (WIDTH - 5) / 5U : hip->cpu_count();

    memset(_vga_console, 0, HEIGHT * WIDTH * VALUEWIDTH);
    _vga_regs.cursor_pos = 0;
    _vga_regs.offset = 0;

    cursor_pos = 4;
    for (unsigned i= cpu_from; i < cpu_to; i++) {
      Vprintf::printf(&_putc, _vga_console, "%4u", i);
      cursor_pos -=1;
    }

    for (unsigned prio=255; prio < 256; prio--) {
      unsigned cpu;
      for (cpu=cpu_from; cpu < cpu_to; cpu++) {
        if (global_prio[cpu][prio]) break;
      }
      if (cpu != cpu_to) {
        cursor_pos = 0;
        Vprintf::printf(&_putc, _vga_console + row * WIDTH * VALUEWIDTH, "%3u", prio);
        cursor_pos -= 1;
        for (unsigned cpu=cpu_from; cpu < cpu_to; cpu++) {
          timevalue rest, val = global_prio[cpu][prio];
          splitfloat(val, rest, cpu);
          if (val >= 100)
            Vprintf::printf(&_putc, _vga_console + row * WIDTH * VALUEWIDTH, " 100");
          else
            Vprintf::printf(&_putc, _vga_console + row * WIDTH * VALUEWIDTH, "%2llu.%1llu", val, rest);

          cursor_pos -= 1;
        }
        memset(_vga_console + row * VALUEWIDTH * WIDTH + cursor_pos * VALUEWIDTH, 0, 1);
        row += 1;
        cursor_pos = 0;
      }
      if (row >= HEIGHT) {
        Vprintf::printf(&_putc, _vga_console + (HEIGHT - 1) * WIDTH * VALUEWIDTH, "more prios available - to many to show ...");
        break;
      }
    }
  }

  void measure_scs(Hip * hip) {
    ClientData volatile * data = &own_scs;
    data->next = &idle_scs;
    idle_scs.next = _storage.next();

    do {
      for (unsigned cpu=0; cpu < hip->cpu_count() ; cpu++) measure(data, cpu);
    } while (data = _storage.next(data));
  }

  void top_dump_scs(Utcb &utcb, Hip * hip, unsigned client_num) {

    memset(_vga_console, 0, HEIGHT * WIDTH * VALUEWIDTH);
    _vga_regs.cursor_pos = 0;
    _vga_regs.offset = 0;

    unsigned cpu_from = 0;
    unsigned cpu_to   = 5U * hip->cpu_count() >= (WIDTH - 32U) ? (WIDTH - 32U) / 5U : hip->cpu_count();

    cursor_pos = 0;
    for (unsigned i=cpu_from; i < cpu_to; i++) {
      Vprintf::printf(&_putc, _vga_console, "%4u", i);
      cursor_pos -= 1;
    }
    memset(_vga_console + cursor_pos * 2, 0, 1);
    cursor_pos = 5 * (1 + cpu_to - cpu_from);
    Vprintf::printf(&_putc, _vga_console + VALUEWIDTH * WIDTH, "idle");
    memset(_vga_console + VALUEWIDTH * WIDTH + cursor_pos * VALUEWIDTH - 1, 0, 1);
    cursor_pos = 0;

    ClientData volatile * data = &own_scs;
    ClientData * client;
    unsigned client_count = HEIGHT - 1, res, more = 0;
    data->next = _storage.next();

    do {
      utcb.head.mtr = 1; //manage utcb by hand (alternative using add_frame, drop_frame which copies unnessary here)
      cursor_pos = 0;
      client = reinterpret_cast<ClientData *>(reinterpret_cast<unsigned long>(data));

      res = get_usage(utcb, client);
      if (client_count > 1) {
        cursor_pos = 5 * (1 + cpu_to - cpu_from);
        Vprintf::printf(&_putc, _vga_console + client_count * VALUEWIDTH * WIDTH, "%s", client->name);
        memset(_vga_console + client_count * VALUEWIDTH * WIDTH + cursor_pos * VALUEWIDTH - 1, 0, 1);
        if (res != ENONE) {
          Vprintf::printf(&_putc, _vga_console + client_count * VALUEWIDTH * WIDTH, "%s", "error");
          continue;
        }

        unsigned i = 1;
        while (i < sizeof(utcb.msg) / sizeof(utcb.msg[0]) && utcb.msg[i] != ~0UL) {
          if (cpu_from <= utcb.msg[i] && utcb.msg[i] < cpu_to) {
            cursor_pos = utcb.msg[i] * 5;
            if (utcb.msg[i+1] >= 100)
              Vprintf::printf(&_putc, _vga_console + client_count * VALUEWIDTH * WIDTH, " 100 ");
            else
              Vprintf::printf(&_putc, _vga_console + client_count * VALUEWIDTH * WIDTH, "%2u.%1u ", utcb.msg[i+1], utcb.msg[i+2]);
            memset(_vga_console + client_count * VALUEWIDTH * WIDTH + cursor_pos * VALUEWIDTH - 1, 0, 1);
          }
          i += 3;
        }
        cursor_pos = 0;
        if (client_count == (HEIGHT - 1 - (client_num % (HEIGHT - 2))))
          _putc(_vga_console + (client_count + 1) * VALUEWIDTH * WIDTH - 1 * VALUEWIDTH, '#');
        client_count --;
      } else {
        more += 1;
        cursor_pos = 0;
        Vprintf::printf(&_putc, _vga_console + VALUEWIDTH * WIDTH * HEIGHT - 12 * VALUEWIDTH, "...%u", more);
      }
    } while (data = _storage.next(data));

    get_idle(hip);
  }

  void top_dump_client(unsigned client_num) {
    unsigned i = 0;
    ClientData volatile * data = &own_scs;
    data->next = _storage.next();

    while (client_num != i && (data = _storage.next(data))) { i++; }
    if (!data) return;

    memset(_vga_console, 0, HEIGHT * WIDTH * VALUEWIDTH);
    _vga_regs.cursor_pos = 0;
    _vga_regs.offset = 0;

    Logging::printf("application: %s\n", data->name);
    Logging::printf("\ncpu prio    quantum  util name\n");
    for (i=0; i < sizeof(data->scs) / sizeof(data->scs[0]); i++) {
      if (!data->scs[i].idx) continue;

      timevalue rest, val = data->scs[i].m_last1 - data->scs[i].m_last2;
      splitfloat(val, rest, data->scs[i].cpu);
      Logging::printf("%3u %4u %10u %3llu.%1llu %s\n", data->scs[i].cpu, data->scs[i].prio, data->scs[i].quantum, val, rest, data->scs[i].name);
    }
  }

  void splitfloat(timevalue &val, timevalue &rest, unsigned cpu) {
    val = val * 1000;
    if (val && global_sum[cpu]) Math::div64(val, global_sum[cpu]);
    if (val < 1000) {
      timevalue util = val;
      if (util && global_sum[cpu]) Math::div64(util, 10U);
      rest = val - util * 10;
      val  = util;
    } else {
      val = 100; rest = 0;
    }
  }

  static unsigned cursor_pos;
  static void _putc(void * data, int value) {
    value &= 0xff;
    if (value == -1 || value == - 2) return; // -1 start, -2 end, can be used for locking
    Screen::vga_putc(0xf00 | value, reinterpret_cast<unsigned short *>(data), cursor_pos);
  }

