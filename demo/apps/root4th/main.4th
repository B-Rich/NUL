\ -*- Mode: Forth -*-

\ Constants

0x1000 constant PAGE

0 constant nIPC-CALL
1 constant nIPC-REPLY
2 constant nCREATE-PD
3 constant nCREATE-EC
4 constant nCREATE-SC
5 constant nCREATE-PT
6 constant nCREATE-SM
7 constant nREVOKE
8 constant nRECALL
9 constant nSEMCTL

0x100 constant nFLAG0

\ HIP

hip 0x1C + q@ constant HIP-EXC
hip 0x24 + q@ constant HIP-GSI

\ Capability allocator

variable empty-cap
HIP-EXC HIP-GSI 0x10 + + empty-cap !

: new-cap ( -- capidx ) empty-cap @ dup 1 + empty-cap ! ;

\ UTCB allocator

variable empty-utcb
utcb-start empty-utcb !

: new-utcb ( -- utcb ) empty-utcb @ dup PAGE + empty-utcb ! ;

\ Syscalls and data types

: map-item ( n -- mtd ) 23 lshift ;

: create-sm { capidx init -- capidx success }
    capidx nCREATE-SM capidx init 0 0 nova-syscall ;

: sem-up   { capidx -- success } nSEMCTL capidx 0 0 0 nova-syscall ;
: sem-down { capidx -- success } [ nSEMCTL nFLAG0 or ] literal capidx 0 0 0 nova-syscall ;

: create-ec { capidx utcb esp -- capidx success }
    capidx [ nCREATE-EC nFLAG0 or ] literal capidx utcb esp 0 nova-syscall ;

: create-pt { capidx ec eip mtd -- capidx success }
    capidx nCREATE-PT capidx ec mtd eip nova-syscall ;

: ipc-call { pt mtd -- success } nIPC-CALL pt mtd 0 0 nova-syscall ;

: checksys ( success -- ) 0= invert if 0xAFF @ then ;

\ A higher level interface

: create-portal { xt stack-size -- pt }
    new-cap new-utcb
     stack-size allocate checksys stack-size + ( ec utcb esp )
    create-ec checksys ( pt )
    new-cap swap
     xt ec-trampoline
     1 map-item
    create-pt checksys
;

\ Root mapper

\ XXX Complete
: root-mapper 0 ( zero items to return ) ;

\ --- M A I N ---

new-cap 1 create-sm checksys constant global-lock

\ Create portal to map physmem and I/O ports
' root-mapper 512 create-portal constant map-pt

map-pt 0 ipc-call checksys

\ We are done.
global-lock sem-down checksys
global-lock sem-down checksys

\ EOF
