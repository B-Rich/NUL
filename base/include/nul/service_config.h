/*
 * Client part of the config protocol.
 *
 * Copyright (C) 2010-2011, Alexander Boettcher <boettcher@tudos.org>
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

/**
 */
struct ConfigProtocol : public GenericProtocol {

  enum {
    TYPE_START_CONFIG = ParentProtocol::TYPE_GENERIC_END,
    TYPE_KILL,
  };

  unsigned start_config(Utcb &utcb, unsigned short &id, unsigned long &maxmem,
                        cap_sel cap_sc_usage, char const * config, unsigned long len = ~0UL)
  {
    unsigned res = call_server(init_frame(utcb, TYPE_START_CONFIG) << Utcb::String(config, len) << Crd(cap_sc_usage, 0, DESC_CAP_ALL), false);
    utcb >> id;
    utcb >> maxmem;
    utcb.drop_frame();
    return res;
  }

  unsigned kill(Utcb &utcb, unsigned short id) {
    return call_server(init_frame(utcb, TYPE_KILL) << id, true);
  }

  ConfigProtocol(unsigned cap_base, unsigned instance=0) : GenericProtocol("config", instance, cap_base, true) {}
};
