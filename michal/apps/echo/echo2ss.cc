/**
 * @file
 * A simple service for educational purposes that does nothing useful;
 * this implementation is based on generic class SServiceProgram.
 *
 * This is probably the simplest possible service. It does not
 * maintain any per-client state and as such it does not require the
 * client to open sessions.
 *
 * Copyright (C) 2011, Michal Sojka <sojka@os.inf.tu-dresden.de>
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
#include <sigma0/console.h>

#include "service_echo.h"
#include <nul/sserviceprogram.h>
#include <wvtest.h>

// This construct can be used to store per-client data
struct EchoClient : public GenericClientData {
  unsigned last_val;		// Last value sent by client
};

class EchoService : public SServiceProgram<EchoClient>
{
  virtual unsigned new_session(EchoClient *client) {return ENONE;}

  virtual unsigned handle_request(EchoClient *client, unsigned op, Utcb::Frame &input, Utcb &utcb, bool &free_cap)
  {
    switch (op) {
    case EchoProtocol::TYPE_ECHO: {
      unsigned value;
      check1(EPROTO, input.get_word(value));
      // Get the value sent by a client
      Logging::printf("echo: Client 0x%x sent us a value %d\n", input.identity(), value);
      client->last_val = value; // Remember the received value
      return value; // "Echo" the received value back
    }
    case EchoProtocol::TYPE_GET_LAST:
      utcb << client->last_val; // Reply with the remembered value (it will appear in utcb.msg[1])
      return ENONE; 		// The returned value will appear in utcb.msg[0]
    default:
      Logging::panic("Unknown op!!!!\n");
      return EPROTO;
    }
  }

public:
  EchoService() : SServiceProgram("Echo service") {
    _console_data.log = new LogProtocol(alloc_cap(LogProtocol::CAP_SERVER_PT + Global::hip.cpu_count()));
    register_service(this, "/echo", Global::hip);
  }
};

ASMFUNCS(EchoService, WvTest)
