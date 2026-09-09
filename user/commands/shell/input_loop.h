#ifndef __USER_SHELL_INPUT_LOOP_H
#define __USER_SHELL_INPUT_LOOP_H

#include <stdbool.h>
#include "../../../common/types/bstring.h"
#include "types/linked_list.h"
#include "interpreter.h"

LinkedListDefine(BString, ShellHistory, bstring_equals);
typedef ShellHistory * ShellHistoryPtr;
typedef const ShellHistory * ConstShellHistoryPtr;

typedef struct InputStream InputStream;
typedef InputStream * InputStreamPtr;
typedef const InputStream * ConstInputStreamPtr;
typedef BString (*InputStreamReadLine)(InputStreamPtr self, bool echo);

typedef struct InputStream
{
    InputStreamReadLine read_line;
} InputStream;

typedef struct Shell
{
    Interpreter interpreter;
    InputStreamPtr input;
    ShellHistory history;
} Shell;

typedef Shell * ShellPtr;
typedef const Shell * ConstShellPtr;

BString InputStream__read_line(InputStreamPtr self, bool echo);
Shell Shell__new(InputStreamPtr input);
ShellResult Shell__run_once(ShellPtr self);
int Shell__run(ShellPtr self);

int input_loop();

#endif
