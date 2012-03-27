/*
 * Copyright (C) 2011-2012, Alexander Boettcher <boettcher@tudos.org>
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

#include <service/string.h> //memset
#include <service/math.h> // htonl, htons
#include <service/logging.h>

#include <nul/service_config.h>
#include <nul/baseprogram.h>

#include <service/endian.h>

#include "server.h"

struct Remcon::server_data * Remcon::check_uuid(char const uuid[UUID_LEN]) {
  unsigned i;
  for(i=0; i < sizeof(server_data)/sizeof(server_data[0]); i++)
    if (server_data[i].id != 0 && !memcmp(server_data[i].uuid, uuid, UUID_LEN)) return &server_data[i];
  return NULL;
}

unsigned Remcon::start_entry(struct Remcon::server_data * entry)
{
  unsigned res, crdout;
  unsigned short id;
  unsigned long mem = 0;
  cap_sel scs_usage = alloc_cap();
  if (!scs_usage) return ERESOURCE;

  res = service_config->start_config(*BaseProgram::myutcb(), id, mem, scs_usage, entry->config, entry->config_len);
  if (res == ENONE) {
    unsigned res_usage;
    unsigned char rrres;
    rrres = nova_syscall(NOVA_LOOKUP, Crd(scs_usage, 0, DESC_CAP_ALL).value(), 0, 0, 0, &crdout); //sanity check that we got a cap
    if (rrres != NOVA_ESUCCESS || crdout == 0) { res = EPERM; goto cleanup; }
    if (ENONE != (res_usage = service_admission->rebind_usage_cap(*BaseProgram::myutcb(), scs_usage))) {
      Logging::printf("failure - rebind of sc usage cap %#x failed: %#x\n", scs_usage, res_usage);
      res = EPERM; goto cleanup;
    }

    entry->scs_usage = scs_usage;
    entry->maxmem    = mem;
    entry->remoteid  = id;
    entry->state     = server_data::status::RUNNING;
  } else goto cleanup;

  return res;

  cleanup:
  dealloc_cap(scs_usage);

  return res;
}

void Remcon::free_entry(struct Remcon::server_data * entry) {
  if (entry->showname) delete [] entry->showname;
  if (entry->config) delete [] entry->config;
  memset(entry, 0, sizeof(entry));
}

struct Remcon::server_data * Remcon::get_free_entry() {
  unsigned i;

  again:

  for(i=0; i < sizeof(server_data)/sizeof(server_data[0]); i++) {
    if(server_data[i].id != 0) continue;
    if (0 != Cpu::cmpxchg4b(&server_data[i].id, 0, i + 1)) goto again;
    return &server_data[i];
  }
  return NULL;
}



void Remcon::recv_call_back(void * in_mem, size_t in_len, void * &out, size_t &out_len) {

  out = 0; out_len = 0;

  memcpy(buf_in, in_mem, data_received + in_len > sizeof(buf_in) ? sizeof(buf_in) - data_received : in_len);
  data_received += in_len;

  if (data_received < sizeof(buf_in)) { return; }

  data_received = 0;
  memset(buf_out, 0, sizeof(buf_out));

  handle_packet();

  if (dowrite) {
    out = buf_out;
    out_len = sizeof(buf_out);
    dowrite = false;
  }
}

#define GET_LOCAL_ID \
        uint32_t * _id = reinterpret_cast<uint32_t *>(&_in->opspecific); \
        uint32_t localid = Math::ntohl(*_id) - 1; \
        if (localid > sizeof(server_data)/sizeof(server_data[0]) || server_data[localid].id == 0) break

#define HIP_COUNT(X, XNUM, Y) \
        mask = 0, num = 0; \
        cpu = Global::hip.cpus(); \
        for (i=0; i < Global::hip.cpu_desc_count(); i++) { \
          if (XNUM == cpu->X) \
            if (!(mask & (1 << cpu->Y))) { \
              mask |= 1 << cpu->Y; \
              num ++; \
            } \
          cpu++; \
        } \
        Y = num

void Remcon::handle_packet(void) {

  //version check
  if (_in->version != Math::htons(DAEMON_VERSION)) {
    _out->version = Math::htons(DAEMON_VERSION);
    _out->result  = NOVA_UNSUPPORTED_VERSION;
    if (sizeof(buf_out) != send(buf_out, sizeof(buf_out)))
      Logging::printf("failure - sending reply\n");

    Logging::printf("failure - outdated protocol request version %#x != %#x current\n",
      Math::htons(_in->version), DAEMON_VERSION);

    return;
  }

  //set default values 
  unsigned op = Math::ntohs(_in->opcode);
  _out->version = Math::htons(DAEMON_VERSION);
  _out->opcode  = Math::htons(op);
  _out->result  = NOVA_OP_FAILED;
  
  switch (op) {
    case NOVA_LIST_ACTIVE_DOMAINS:
      {
        unsigned i, k = 0;
        uint32_t * ids = reinterpret_cast<uint32_t *>(&_out->opspecific);

        for(i=0; i < sizeof(server_data)/sizeof(server_data[0]); i++) {
          if (server_data[i].id == 0 || server_data[i].state == server_data::status::OFF) continue;

          k++;
          ids[k] = Math::htonl(server_data[i].id);
  
          if (reinterpret_cast<unsigned char *>(&ids[k + 1]) >= buf_out + NOVA_PACKET_LEN) break; //no space left
        }
        ids[0] = Math::htonl(k); //num of ids
        _out->result  = NOVA_OP_SUCCEEDED;
        break;
      }
    case NOVA_NUM_OF_ACTIVE_DOMAINS:
      {
        unsigned i, k = 0;
        for(i=0; i < sizeof(server_data)/sizeof(server_data[0]); i++) {
          if (server_data[i].id == 0) continue;
          k++;
        }

        uint32_t _num = Math::htonl(k);
        memcpy(&_out->opspecific, &_num, 4);
        _out->result  = NOVA_OP_SUCCEEDED;
        break;
      }
    case NOVA_NUM_OF_DEFINED_DOMAINS:
      {
        unsigned i, k = 0;
        for(i=0; i < sizeof(server_data)/sizeof(server_data[0]); i++) {
          if (server_data[i].id == 0 || server_data[i].state != server_data::status::OFF) continue;
          k++;
        }

        uint32_t num = Math::htonl(k);
        memcpy(&_out->opspecific, &num, 4);
        _out->result  = NOVA_OP_SUCCEEDED;
        break;
      }
    case NOVA_LIST_DEFINED_DOMAINS:
      {
        uint32_t * num = reinterpret_cast<uint32_t *>(&_out->opspecific);
        unsigned char * names = reinterpret_cast<unsigned char *>(&_out->opspecific) + sizeof(uint32_t);

        unsigned i,k = 0;
        for(i=0; i < sizeof(server_data)/sizeof(server_data[0]); i++) {
          if (server_data[i].id == 0 || server_data[i].state != server_data::status::OFF) continue;
          if (names + server_data[i].showname_len + 1 > buf_out + NOVA_PACKET_LEN) {
            Logging::printf("no space left\n");
            break; //no space left
          }

          memcpy(names, server_data[i].showname, server_data[i].showname_len);
          names[server_data[i].showname_len] = 0;
          names += server_data[i].showname_len + 1;
          k++; 
        }
        *num = Math::htonl(k);
        _out->result  = NOVA_OP_SUCCEEDED;

        break;
      }
    case NOVA_GET_NAME:
      {
        unsigned char * _name = reinterpret_cast<unsigned char *>(&_in->opspecific);

        unsigned i, len;
        for(i=0; i < sizeof(server_data)/sizeof(server_data[0]); i++) {
          if (server_data[i].id == 0 || server_data[i].state != server_data::status::OFF) continue;
          len = server_data[i].showname_len;
          if (buf_in + NOVA_PACKET_LEN <=_name + len || _name[len] != 0) continue;
          if (memcmp(server_data[i].showname, _name, len)) continue;
          if (&_out->opspecific + UUID_LEN + len + 1 > buf_out + NOVA_PACKET_LEN) break; //no space left
      
          memcpy(&_out->opspecific, server_data[i].uuid, UUID_LEN);
          memcpy(&_out->opspecific + UUID_LEN, server_data[i].showname, len);
          *(&_out->opspecific + UUID_LEN + len) = 0;
          _out->result  = NOVA_OP_SUCCEEDED;
          break;
        }
        break;
      }
    case NOVA_GET_NAME_UUID:
      {
        struct server_data * entry = check_uuid(reinterpret_cast<char *>(&_in->opspecific));
        if (!entry) break;

        unsigned len = entry->showname_len + 1;
        uint32_t id = Math::htonl(entry->id);
        memcpy(&_out->opspecific, &id, sizeof(id));
        memcpy(&_out->opspecific + sizeof(id), entry->showname, len);
        *(&_out->opspecific + sizeof(id) + len - 1) = 0;
        _out->result  = NOVA_OP_SUCCEEDED;
      }
    case NOVA_GET_NAME_ID:
      {
        GET_LOCAL_ID;
        if (server_data[localid].state == server_data::status::OFF) break;

        unsigned len = server_data[localid].showname_len + 1;
        if (&_out->opspecific + UUID_LEN + len > buf_out + NOVA_PACKET_LEN) break; // no space left

        memcpy(&_out->opspecific, server_data[localid].uuid, UUID_LEN);
        memcpy(&_out->opspecific + UUID_LEN, server_data[localid].showname, len);
        *(&_out->opspecific + UUID_LEN + len - 1) = 0;
        _out->result  = NOVA_OP_SUCCEEDED;
        break;
      }
    case NOVA_VM_START:
      {
        struct tmp0 {
          char const uuid[UUID_LEN];
        } PACKED * tmp = reinterpret_cast<struct tmp0 *>(&_in->opspecific);
        struct tmp1 {
          char const uuid[UUID_LEN];
          uint32_t maxmem;
          uint32_t showname_len;
          uint64_t disk_size;
          char showname;
        } PACKED * client = 0;

        unsigned filelen, res = EABORT;
        char * module = 0;
        char const * file = 0;
        FsProtocol::dirent fileinfo;
        unsigned portal_num = FsProtocol::CAP_SERVER_PT + cpu_count;
        cap_sel cap_base  = alloc_cap(portal_num);
        if (!cap_base) break;
        FsProtocol * fs_obj = 0;
        FsProtocol::File * file_obj = 0;

        struct server_data * entry = Remcon::get_free_entry();
        if (!entry) break;
        struct server_data * etemplate = check_uuid(tmp->uuid);

        if (!etemplate) {
          client = reinterpret_cast<struct tmp1 *>(&_in->opspecific);
          client->showname_len = Math::ntohl(client->showname_len);
          if (client->showname_len > NOVA_PACKET_LEN ||
              (&client->showname + client->showname_len > reinterpret_cast<char *>(buf_in) + NOVA_PACKET_LEN)) goto ecleanup; // cheater!
          if (!this->file_template || !this->file_len_template) { Logging::printf("failure - no disk template configured\n"); goto ecleanup; }

          memcpy(entry->uuid, client->uuid, UUID_LEN);
          char * _name = new char [client->showname_len];
          if (!_name) goto ecleanup;

          memcpy(_name, &client->showname, client->showname_len);
          memcpy(entry->uuid, client->uuid, UUID_LEN);
          entry->showname     = _name;
          entry->showname_len = client->showname_len;
          entry->maxmem       = Math::ntohl(client->maxmem) * 1024;

          unsigned proto_len = sizeof(entry->fsname) - 3;
          memcpy(entry->fsname, "fs/", 3);
          file = FsProtocol::parse_file_name(this->file_template, entry->fsname + 3, proto_len);
          if (!file) { Logging::printf("failure - name of file misformatted %s\n", this->file_template); goto ecleanup; }
          filelen = this->file_len_template - (file - this->file_template);
        } else {
          if (!etemplate->istemplate) goto ecleanup; //only templates may be instantiated and started
          if (check_uuid(tmp->uuid + UUID_LEN)) goto ecleanup; //if we have this 'new' uuid already deny starting

          memcpy(entry->uuid, tmp->uuid + UUID_LEN, UUID_LEN);
          entry->showname_len = etemplate->showname_len + 8;
          char * tmp     = new char [entry->showname_len];
          if (!tmp) goto ecleanup;
          memcpy(tmp, etemplate->showname, etemplate->showname_len);
          Vprintf::snprintf(tmp + etemplate->showname_len, 8, " %4u", entry->id);
          tmp[entry->showname_len - 1] = 0;
          entry->showname = tmp;

          memcpy(entry->fsname, etemplate->fsname, sizeof(etemplate->fsname));
          file = etemplate->config;
          filelen = etemplate->config_len;
        }

        fs_obj   = new FsProtocol(cap_base, entry->fsname);
        if (!fs_obj) goto cleanup;
        file_obj = new FsProtocol::File(*fs_obj, alloc_cap());
        if (!file_obj) goto cleanup;

        if ((res = fs_obj->get(*BaseProgram::myutcb(), *file_obj, file, filelen)) ||
            (res = file_obj->get_info(*BaseProgram::myutcb(), fileinfo)))
        {
          Logging::printf("failure - err=0x%x, config %30s could not load file\n '%s'\n", res, entry->showname, file);
          goto cleanup;
        }

        module = new(4096) char[fileinfo.size];
        if (!module) goto cleanup;

        res = file_obj->copy(*BaseProgram::myutcb(), module, fileinfo.size);
        if (res != ENONE) goto cleanup;

        entry->config       = module;
        entry->config_len   = fileinfo.size;

        if (!etemplate) {
          char * replaceuuid = strstr(module, "disk::XXXX:XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX");
          if (!replaceuuid) { Logging::printf("failure - template config is misformatted\n%s\n", module); goto cleanup; }
          replaceuuid += sizeof("disk::") - 1;

//            entry->disks // XXX don't assume only one disk
          entry->disks[0].size = Endian::hton64(client->disk_size); //actual ntoh64
          entry->disks[0].internal.diskid = get_free_disk(entry->disks[0].size, entry->disks[0].internal.sectorsize,
                                                          replaceuuid, 41);
          if (entry->disks[0].internal.diskid == ~0U) {
            Logging::printf("failure - no free disk with enough space - %llu Byte\n", entry->disks[0].size);
            goto cleanup;
          }

          entry->state = server_data::status::BLOCKED;
          uint32_t id = Math::htonl(entry->id);
          memcpy(&_out->opspecific, &id, sizeof(id));
          _out->result  = NOVA_OP_SUCCEEDED;
          break;
        } else {
          entry->disks[0].size = 0; //XXX
          entry->disks[0].internal.diskid = ~0U; //XXX
        }

        res = start_entry(entry);
        if (res == ENONE) {
          uint32_t id = Math::htonl(entry->id);
          memcpy(&_out->opspecific, &id, sizeof(id));
          _out->result  = NOVA_OP_SUCCEEDED;
        } else {
          if (res == ConfigProtocol::ECONFIGTOOBIG)
            Logging::printf("failure - configuration '%10s' is to big (size=%llu)\n", entry->showname, fileinfo.size);
          else if (res == ERESOURCE) {
            _out->result  = NOVA_OP_FAILED_OUT_OF_MEMORY;
            Logging::printf("failure - out of memory\n");
          }
        }

        cleanup:

        //cap_base is deallocated in fs_obj.destroy
        if (fs_obj) fs_obj->destroy(*BaseProgram::myutcb(), portal_num, this);
        if (file_obj) delete file_obj;
        if (fs_obj) delete fs_obj;

        if (res != ENONE) {
          Logging::printf("failure - starting VM - reason : %#x\n", res);
          free_entry(entry);
        }

        break;

        ecleanup:
        free_entry(entry);
        break;
      }
    case NOVA_VM_DESTROY:
      {
        GET_LOCAL_ID;
        unsigned res = service_config->kill(*BaseProgram::myutcb(), server_data[localid].remoteid);
        if (res == ENONE) {
          if (server_data[localid].config) delete [] server_data[localid].config;
          if (server_data[localid].disks[0].internal.diskid != ~0U) {
            bool freedisk = clean_disk(server_data[localid].disks[0].internal.diskid); //XXX more than one disk !!!
            if (!freedisk) Logging::printf("failure  - freeing internal disk %u\n", server_data[localid].disks[0].internal.diskid);
          }
          memset(&server_data[localid], 0, sizeof(server_data[localid]));
          _out->result  = NOVA_OP_SUCCEEDED;
        }
        break;
      }
    case NOVA_GET_VM_INFO:
      {
        struct server_data * entry = check_uuid(reinterpret_cast<char *>(&_in->opspecific));
        if (!entry) break;
       
        uint64 consumed_time = 0;
        if (entry->state != server_data::status::OFF &&
            ENONE != service_admission->get_statistics(*BaseProgram::myutcb(), entry->scs_usage, consumed_time)) {
          Logging::printf("failure - could not get consumed time of client\n");
          //break;
        }

        char  * ptr = reinterpret_cast<char *>(&_out->opspecific);
        *reinterpret_cast<uint32_t *>(ptr) = Math::htonl(entry->maxmem / 1024); //in kB
        *reinterpret_cast<uint32_t *>(ptr += sizeof(uint32_t)) = Math::htonl(1); //vcpus
        *reinterpret_cast<uint64_t *>(ptr += sizeof(uint32_t)) = (0ULL + Math::htonl(consumed_time)) << 32 + Math::htonl(consumed_time >> 32); //in microseconds
        *reinterpret_cast<uint32_t *>(ptr += sizeof(uint64_t)) = Math::htonl(entry->state);
        _out->result  = NOVA_OP_SUCCEEDED;

        break;
      }
    case NOVA_ENABLE_EVENT:
    case NOVA_DISABLE_EVENT:
      {
        uint32_t * _id = reinterpret_cast<uint32_t *>(&_in->opspecific);
        uint32_t localid = Math::ntohl(*_id);
        uint32_t eventid = Math::ntohl(*(_id + 1));

        if (localid == ~0U) {
          gevents = (op == NOVA_ENABLE_EVENT);
          _out->result  = NOVA_OP_SUCCEEDED;
          break;
        }

        localid -= 1;
        if (localid > sizeof(server_data)/sizeof(server_data[0]) || server_data[localid].id == 0) break;
        unsigned ij = chg_event_slot(&server_data[localid], eventid, op == NOVA_ENABLE_EVENT);
        if (ij == ~0U) break;
        //Logging::printf("event enabled %u\n", eventid);
        _out->result  = NOVA_OP_SUCCEEDED;
        break;
      }
    case NOVA_ATOMIC_RULE:
      {
        GET_LOCAL_ID;
        uint32_t eventid  = Math::ntohl(*(_id + 1));
        uint32_t actionid = Math::ntohl(*(_id + 2));

        unsigned ij = get_event_slot(&server_data[localid], eventid);
        if (~0U == ij) break;

        server_data[localid].events[ij].action = actionid;
        _out->result  = NOVA_OP_SUCCEEDED;
        break;
      }
    case NOVA_HW_INFO:
      {
        unsigned long mask = 0, num = 0, i, lookfor;
        uint32_t core, thread, package;

        Hip_cpu const * cpu = Global::hip.cpus();
        for (i=0; i < Global::hip.cpu_desc_count(); i++) {
          if (!(mask & (1 << cpu->package))) {
            mask |= 1 << cpu->package;
            num += 1;
          }
          cpu++;
        }
        package = num;

        lookfor = Cpu::bsr(mask); //XXX check whether all packages has same number of cores?
        HIP_COUNT(package, lookfor, core);

        lookfor = Cpu::bsr(mask); //XXX check whether all cores has same number of threads?
        HIP_COUNT(core, lookfor, thread);

        //Logging::printf("packages %u, cores %u, threads %u, vendor ", package, core, thread);
        unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
        eax = Cpu::cpuid(eax, ebx, ecx, edx);

        uint32_t * data = reinterpret_cast<uint32_t *>(&_out->opspecific);
        *data++ = Math::htonl(0); //memory size kb
        *data++ = Math::htonl(Global::hip.cpu_count()); //active cpus
        *data++ = Math::htonl(Global::hip.freq_tsc / 1000); //freq in MHz
        *data++ = Math::htonl(1); //NUMA nodes
        *data++ = Math::htonl(package); *data++ = Math::htonl(core); *data++ = Math::htonl(thread);

        //char * name = reinterpret_cast<char *>(data);
        *data++ = ebx; *data++ = edx; *data++ = ecx; *data++ = 0;
        //be aware not to exceed NOVA_PACKET_LEN here if you add a longer string !
        //Logging::printf("%s\n", name);

        _out->result  = NOVA_OP_SUCCEEDED;
      }
      break;
    case NOVA_AUTH:
      {
        uint32_t len = Math::ntohl(*reinterpret_cast<uint32_t *>(&_in->opspecific));
        unsigned char * authid = reinterpret_cast<unsigned char *>(&_in->opspecific) + sizeof(len);
        if (len < 2 || len > NOVA_PACKET_LEN || &_in->opspecific + len > buf_in + NOVA_PACKET_LEN) break; // cheater!

        authid[len - 1] = 0;
        Logging::printf("%u %s\n", len, authid);
      }
      break;
    default:
      Logging::printf("got bad packet op=%u\n", op);
  }

  if (sizeof(buf_out) != send(buf_out, sizeof(buf_out))) {
    Logging::printf("failure - sending reply\n");
    return;
  }
}
