/** @file
 * Client part of the log protocol.
 *
 * Copyright (C) 2010, Bernhard Kauer <bk@vmmon.org>
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
#pragma once

#include "parent.h"
#include "host/dma.h"
#include "sigma0/consumer.h"

#define DISK_SERVICE_IN_S0

/**
 * Client part of the disk protocol.
 * Missing: register shared memory producer/consumer.
 */
struct DiskProtocol : public GenericProtocol {
  enum {
    MAXDISKREQUESTS    = 32  // max number of outstanding disk requests per client
  };
  enum {
    TYPE_GET_PARAMS = ParentProtocol::TYPE_GENERIC_END,
    TYPE_READ,
    TYPE_WRITE,
    TYPE_FLUSH_CACHE,
    TYPE_GET_COMPLETION,
    TYPE_GET_MEM_PORTAL,
    TYPE_DMA_BUFFER,
  };

  typedef Consumer<MessageDiskCommit, DiskProtocol::MAXDISKREQUESTS> DiskConsumer;
  typedef Producer<MessageDiskCommit, DiskProtocol::MAXDISKREQUESTS> DiskProducer;

  DiskConsumer *consumer;
  KernelSemaphore *sem;

  unsigned get_params(Utcb &utcb, DiskParameter *params) {
    unsigned res;
    if (!(res = call_server_keep(init_frame(utcb, TYPE_GET_PARAMS))))
      if (utcb >> *params)  res = EPROTO;
    utcb >> *params;
    utcb.drop_frame();
    return res;
  }

  unsigned attach(Utcb &utcb, void *dma_addr, size_t dma_size, cap_sel tmp_cap,
		  DiskConsumer *consumer, KernelSemaphore *notify_sem) {
    unsigned backup_crd = utcb.head.crd;
    unsigned res;

    assert((reinterpret_cast<unsigned long>(dma_addr) & ((1ul<<12)-1)) == 0);
    assert((reinterpret_cast<unsigned long>(consumer) & ((1<<12)-1)) == 0);
    assert(sizeof(*consumer) < 1<<12);

    this->consumer = consumer;
    this->sem = notify_sem;

    /* Delegate sempahore and request portal capability for memory delegation */
    init_frame(utcb, TYPE_GET_MEM_PORTAL) << Utcb::TypedMapCap(notify_sem->sm());
    utcb.head.crd = Crd(tmp_cap, 0, DESC_CAP_ALL).value();
    res = call_server_drop(utcb);
    check2(err, res);

    /* Delegate the memory via the received portal */
    utcb.add_frame();
#ifdef DISK_SERVICE_IN_S0
    utcb << dma_size;
    utcb << Utcb::TypedTranslateMem(consumer, 0);
    assert(!utcb.add_mappings(reinterpret_cast<mword>(dma_addr), dma_size, reinterpret_cast<mword>(dma_addr), DESC_MEM_ALL));
#else
#warning TODO
    utcb << Utcb::TypedDelegateMem(consumer, 0);
    assert(!utcb.add_mappings(reinterpret_cast<mword>(dma_addr), dma_size, hotstop | MAP_MAP, DESC_MEM_ALL));
#endif
    res = nova_call(tmp_cap);
    utcb.drop_frame();
  err:
    utcb.head.crd = backup_crd;
    return res;
  }

  unsigned read_write(Utcb &utcb, bool read, unsigned disk, unsigned long usertag, unsigned long long sector,
		unsigned dmacount, DmaDescriptor *dma)
  {
    init_frame(utcb, read ? TYPE_READ : TYPE_WRITE) << disk << usertag << sector << dmacount;
    for (unsigned i=0; i < dmacount; i++)  utcb << dma[i];
    return call_server_drop(utcb);
  }

  unsigned read(Utcb &utcb, unsigned disk, unsigned long usertag, unsigned long long sector, unsigned dmacount, DmaDescriptor *dma)
  { return read_write(utcb, true, disk, usertag, sector, dmacount, dma); }

  unsigned write(Utcb &utcb, unsigned disk, unsigned long usertag, unsigned long long sector, unsigned dmacount, DmaDescriptor *dma)
  { return read_write(utcb, false, disk, usertag, sector, dmacount, dma); }

  unsigned flush_cache(Utcb &utcb) {
    return call_server_drop(init_frame(utcb, TYPE_FLUSH_CACHE));
  }


  unsigned get_completion(Utcb &utcb, unsigned &tag, unsigned &status) {
    unsigned res;
    if (!(res = call_server_keep(init_frame(utcb, TYPE_GET_COMPLETION))))
      if (utcb >> tag || utcb >> status)  res = EPROTO;
    utcb.drop_frame();
    return res;
  }

  DiskProtocol(unsigned cap_base, unsigned instance) : GenericProtocol("disk", instance, cap_base, true) {}
};
