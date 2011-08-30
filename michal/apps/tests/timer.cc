/**
 * @file
 * Test application for disk service.
 *
 * Copyright (C) 2011 Michal Sojka <sojka@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of Vancouver.nova.
 *
 * Vancouver.nova is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * Vancouver.nova is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include <wvprogram.h>
#include <nul/service_timer.h>

class TimerTest : public WvProgram
{
public:
  void wvrun(Utcb *utcb, Hip *hip)
  {
    Clock *clock = new Clock(hip->freq_tsc * 1000);
    TimerProtocol *timer_service = new TimerProtocol(alloc_cap_region(TimerProtocol::CAP_SERVER_PT + hip->cpu_desc_count(), 0));

    WVPASS(clock);
    WVPASS(timer_service);

    unsigned t1, t2;

    t1 = clock->time();
    TimerProtocol::MessageTimer msg(clock->abstime(100, 1000)); // 100 ms
    WVNUL(timer_service->timer(*utcb, msg));
    KernelSemaphore timersem = KernelSemaphore(timer_service->get_notify_sm());
    
    timersem.downmulti();
    t2 = clock->time();
    int sleep_time_ms = Math::muldiv128(t2 - t1, 1000, hip->freq_tsc * 1000);

    WVPASSGE(sleep_time_ms, 100); // Broken in qemu
  }
};

ASMFUNCS(TimerTest, WvTest)
