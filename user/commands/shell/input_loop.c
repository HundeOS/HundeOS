#include "../../user.h"
#include "input_loop.h"

LinkedListBodyDefine(ShellHistory);

typedef struct ConsoleInputStream
{
    InputStream input;
    ShellHistoryPtr history;
}
ConsoleInputStream;

typedef ConsoleInputStream * ConsoleInputStreamPtr;

static const BString SHELL_PROMPT = BSTRING_NEW("\n> ");
static const BString SHELL_REDRAW_PREFIX = BSTRING_NEW("\r\033[2K> ");
static const BString EMPTY_LINE = BSTRING_NEW("");
static BString ConsoleInputStream__read_line(InputStreamPtr self, bool echo);
static void ConsoleInputStream__bind_history(ConsoleInputStreamPtr self, ShellHistoryPtr history);
static void ConsoleInputStream__redraw_line(ConstBStringPtr line, StringPos cursor, bool echo);
static bool ConsoleInputStream__read_char(char * target);
static bool ConsoleInputStream__read_escape_sequence(char * first, char * second);
static bool Shell__should_store_history(ConstBStringPtr line);
static void Shell__store_history(ShellPtr self, ConstBStringPtr line);

static ConsoleInputStream ConsoleInputStream__new(void)
{
    ConsoleInputStream input = {
        .input = {
            .read_line = ConsoleInputStream__read_line
        },
        .history = NULL,
    };
    return input;
}

static void ConsoleInputStream__bind_history(ConsoleInputStreamPtr self, ShellHistoryPtr history)
{
    self->history = history;
}

static BString ConsoleInputStream__read_line(InputStreamPtr self, bool echo)
{
    ConsoleInputStreamPtr input = (ConsoleInputStreamPtr) self;
    BString line = EMPTY_BSTRING;
    BString draft = EMPTY_BSTRING;
    ShellHistory__NodePtr history_cursor = NULL;
    bool draft_saved = false;
    StringPos cursor = 0;

    while (true)
    {
        char ch = '\0';
        if (!ConsoleInputStream__read_char(&ch))
        {
            return line;
        }

        if (ch == '\r' || ch == '\n')
        {
            return line;
        }

        if (ch == '\b' || ch == 0x7f)
        {
            if (history_cursor != NULL)
            {
                history_cursor = NULL;
            }

            if (cursor > 0)
            {
                if (BString__remove_at(&line, cursor - 1))
                {
                    cursor--;
                }
            }
            ConsoleInputStream__redraw_line(&line, cursor, echo);
            continue;
        }

        if (ch == '\x1b')
        {
            char first = '\0';
            char second = '\0';
            if (!ConsoleInputStream__read_escape_sequence(&first, &second))
            {
                continue;
            }

            if (first != '[')
            {
                continue;
            }

            if (second == 'A')
            {
                if (input->history == NULL || ShellHistory__len(input->history) == 0)
                {
                    continue;
                }

                if (!draft_saved)
                {
                    draft = line;
                    draft_saved = true;
                }

                if (history_cursor == NULL)
                {
                    history_cursor = input->history->tail;
                }
                else if (history_cursor->prev != NULL)
                {
                    history_cursor = history_cursor->prev;
                }

                if (history_cursor != NULL)
                {
                    line = history_cursor->value;
                    cursor = line.len;
                    ConsoleInputStream__redraw_line(&line, cursor, echo);
                }
            }
            else if (second == 'B')
            {
                if (input->history == NULL || ShellHistory__len(input->history) == 0)
                {
                    continue;
                }

                if (history_cursor == NULL)
                {
                    continue;
                }

                if (history_cursor->next != NULL)
                {
                    history_cursor = history_cursor->next;
                    line = history_cursor->value;
                    cursor = line.len;
                }
                else
                {
                    history_cursor = NULL;
                    line = draft_saved ? draft : EMPTY_LINE;
                    cursor = line.len;
                }

                ConsoleInputStream__redraw_line(&line, cursor, echo);
            }
            else if (second == 'C')
            {
                if (cursor < line.len)
                {
                    if (history_cursor != NULL)
                    {
                        history_cursor = NULL;
                    }
                    cursor++;
                    ConsoleInputStream__redraw_line(&line, cursor, echo);
                }
            }
            else if (second == 'D')
            {
                if (cursor > 0)
                {
                    if (history_cursor != NULL)
                    {
                        history_cursor = NULL;
                    }
                    cursor--;
                    ConsoleInputStream__redraw_line(&line, cursor, echo);
                }
            }

            continue;
        }

        if (history_cursor != NULL)
        {
            history_cursor = NULL;
        }

        if (BString__insert_char(&line, cursor, ch))
        {
            cursor++;
            ConsoleInputStream__redraw_line(&line, cursor, echo);
        }
    }
}

static bool ConsoleInputStream__read_char(char * target)
{
    return console_read(target, 1) == 1;
}

static bool ConsoleInputStream__read_escape_sequence(char * first, char * second)
{
    return ConsoleInputStream__read_char(first) && ConsoleInputStream__read_char(second);
}

static void ConsoleInputStream__redraw_line(ConstBStringPtr line, StringPos cursor, bool echo)
{
    if (!echo)
    {
        return;
    }

    print(BString__cstr(&SHELL_REDRAW_PREFIX));
    print(BString__cstr(line));

    StringPos move_left = line->len - cursor;
    if (move_left > 0)
    {
        printf("\033[%iD", (int) move_left);
    }
}

static bool Shell__should_store_history(ConstBStringPtr line)
{
    BString stripped = BString__strip(line);
    return !BString__is_empty(&stripped);
}

static void Shell__store_history(ShellPtr self, ConstBStringPtr line)
{
    if (!Shell__should_store_history(line))
    {
        return;
    }

    ResultMatch(CollectionCode, ShellHistory__push(&self->history, *line))
        IsError(code, message, {})
    End
}

static void Shell__handle_result(ConstShellPtr self, ShellResult result)
{
    switch (result)
    {
        case EXIT:
            print("Shell exiting\n");
            break;
        case COMMAND_NOT_FOUND:
            print("Command not found\n");
            break;
        case COMMAND_FAILED:
            print("Command failed\n");
            break;
        case COMMAND_EXECUTED:
        case COMMAND_SUCCESS:
            break;
    }
}

BString InputStream__read_line(InputStreamPtr self, bool echo)
{
    return self->read_line(self, echo);
}

Shell Shell__new(InputStreamPtr input)
{
    Shell shell = {
        .interpreter = Interpreter__new(),
        .input = input,
        .history = ShellHistory__new(),
    };
    return shell;
}

ShellResult Shell__run_once(ShellPtr self)
{
    print(BString__cstr(&SHELL_PROMPT));
    BString line = InputStream__read_line(self->input, true);
    print("\n");

    ShellResult result = Interpreter__interpret(&self->interpreter, &line);
    Shell__store_history(self, &line);
    Shell__handle_result(self, result);
    return result;
}

int Shell__run(ShellPtr self)
{
    while (true)
    {
        if (Shell__run_once(self) == EXIT)
        {
            return 0;
        }
    }
}

int input_loop()
{
    ConsoleInputStream input = ConsoleInputStream__new();
    Shell shell = Shell__new(&input.input);
    ConsoleInputStream__bind_history(&input, &shell.history);
    return Shell__run(&shell);
}
