#include <ficl.h>
#include "sigma0/console.h"

#include <stdio.h>

extern "C" int
putchar(int c)
{
  Logging::printf("%c", c);
  return c;
}

class Forth : public ProgramConsole
{
public:
  int run(Hip *hip)
  {
    console_init("Forth");
    printf("Forth booting...\n");

    FICL_SYSTEM *pSys = ficlInitSystem(10000);
    printf("ficlInitSystem OK -> %p\n", pSys);
    // XXX Add new words here.
    FICL_VM *pVM = ficlNewVM(pSys);
    printf("ficlNewVM OK -> %p\n", pVM);

    ficlEvaluate(pVM, ".ver .( " __DATE__ " ) cr quit");

    return 0;
  }
};

ASMFUNCS(Forth, NovaProgram);
