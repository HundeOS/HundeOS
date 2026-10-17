#ifndef __USER_PROGRAMS_WUFF_LD_INTERPRETER_H
#define __USER_PROGRAMS_WUFF_LD_INTERPRETER_H

#include "../../lib/wuff/loader.h"

typedef struct WuffInterpreterProgram
{
    WuffGraph graph;
    Address entry;
} WuffInterpreterProgram;

typedef WuffInterpreterProgram * WuffInterpreterProgramPtr;
typedef const WuffInterpreterProgram * ConstWuffInterpreterProgramPtr;

ResultDef(WuffInterpreterProgram);

WuffInterpreterProgram WuffInterpreterProgram__new(WuffGraph graph, Address entry);
Result(WuffInterpreterProgram) WuffInterpreter__load(ConstBStringPtr path);
Address WuffInterpreterProgram__entry(ConstWuffInterpreterProgramPtr self);
WuffGraphPtr WuffInterpreterProgram__graph(WuffInterpreterProgramPtr self);

#endif
