#include "../../user.h"
#include "input_loop.h"

LinkedListBodyDefine(ShellHistory);

typedef struct ConsoleInputStream
{
    InputStream input;
    ShellHistoryPtr history;
} ConsoleInputStream;

typedef ConsoleInputStream * ConsoleInputStreamPtr;

static const BString SHELL_PROMPT = bstr("\n> ");
static const BString SHELL_REDRAW_PREFIX = bstr("\r\033[2K> ");
static const BString EMPTY_LINE = bstr("");
static BString ConsoleInputStream__read_line(InputStreamPtr self, bool echo);
static void ConsoleInputStream__bind_history(ConsoleInputStreamPtr self, ShellHistoryPtr history);
static void ConsoleInputStream__redraw_line(ConstBStringPtr line, StringPos cursor, bool echo);
static Optional(char) ConsoleInputStream__read_char(void);
static Optional(BString) ConsoleInputStream__read_escape_sequence(void);
static bool Shell__should_store_history(ConstBStringPtr line);
static void Shell__store_history(ShellPtr self, ConstBStringPtr line);

static ConsoleInputStream ConsoleInputStream__new(void)
{
    ConsoleInputStream input = {
        .input = {.read_line = ConsoleInputStream__read_line},
        .history = NULL, // hundelint: ignore-no-null
    };
    return input;
}

static void ConsoleInputStream__bind_history(ConsoleInputStreamPtr self, ShellHistoryPtr history)
{
    self->history = history;
}

static BString ConsoleInputStream__read_line(InputStreamPtr self, bool echo)
{
    ConsoleInputStreamPtr input = (ConsoleInputStreamPtr)self;
    BString line = empty_bstr();
    BString draft = empty_bstr();
    ShellHistory__NodePtr history_cursor = NULL; // hundelint: ignore-no-null
    bool draft_saved = false;
    StringPos cursor = 0;

    while (true)
    {
        char ch = '\0';
        match_optional(
            ConsoleInputStream__read_char(),
            some(read_ch) { ch = read_ch; },
            none()
            {
                return line;
                ;
            }
        );

        if (ch == '\r' || ch == '\n')
        {
            return line;
        }

        if (ch == '\b' || ch == 0x7f)
        {
            if (history_cursor != NULL) // hundelint: ignore-no-null
            {
                history_cursor = NULL; // hundelint: ignore-no-null
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
            BString escape_sequence = empty_bstr();
            match_optional(
                ConsoleInputStream__read_escape_sequence(),
                some(sequence) { escape_sequence = sequence; },
                none()
                {
                    continue;
                    ;
                }
            );

            char first = BString__char_at_or(&escape_sequence, 0, '\0');
            char second = BString__char_at_or(&escape_sequence, 1, '\0');
            if (first != '[')
            {
                continue;
            }

            if (second == 'A')
            {
                if (input->history == NULL || ShellHistory__len(input->history) == 0) // hundelint: ignore-no-null
                {
                    continue;
                }

                if (!draft_saved)
                {
                    draft = line;
                    draft_saved = true;
                }

                if (history_cursor == NULL) // hundelint: ignore-no-null
                {
                    history_cursor = input->history->tail;
                }
                else if (history_cursor->prev != NULL) // hundelint: ignore-no-null
                {
                    history_cursor = history_cursor->prev;
                }

                if (history_cursor != NULL) // hundelint: ignore-no-null
                {
                    line = history_cursor->value;
                    cursor = BString__length(&line);
                    ConsoleInputStream__redraw_line(&line, cursor, echo);
                }
            }
            else if (second == 'B')
            {
                if (input->history == NULL || ShellHistory__len(input->history) == 0) // hundelint: ignore-no-null
                {
                    continue;
                }

                if (history_cursor == NULL) // hundelint: ignore-no-null
                {
                    continue;
                }

                if (history_cursor->next != NULL) // hundelint: ignore-no-null
                {
                    history_cursor = history_cursor->next;
                    line = history_cursor->value;
                    cursor = BString__length(&line);
                }
                else
                {
                    history_cursor = NULL; // hundelint: ignore-no-null
                    line = draft_saved ? draft : EMPTY_LINE;
                    cursor = BString__length(&line);
                }

                ConsoleInputStream__redraw_line(&line, cursor, echo);
            }
            else if (second == 'C')
            {
                if (cursor < BString__length(&line))
                {
                    if (history_cursor != NULL) // hundelint: ignore-no-null
                    {
                        history_cursor = NULL; // hundelint: ignore-no-null
                    }
                    cursor++;
                    ConsoleInputStream__redraw_line(&line, cursor, echo);
                }
            }
            else if (second == 'D')
            {
                if (cursor > 0)
                {
                    if (history_cursor != NULL) // hundelint: ignore-no-null
                    {
                        history_cursor = NULL; // hundelint: ignore-no-null
                    }
                    cursor--;
                    ConsoleInputStream__redraw_line(&line, cursor, echo);
                }
            }

            continue;
        }

        if (history_cursor != NULL) // hundelint: ignore-no-null
        {
            history_cursor = NULL; // hundelint: ignore-no-null
        }

        bool append_at_end = cursor == BString__length(&line);
        if (BString__insert_char(&line, cursor, ch))
        {
            cursor++;
            if (append_at_end)
            {
                if (echo)
                {
                    BString output = empty_bstr();
                    BString__push_char(&output, ch);
                    IO__print(&output);
                }
            }
            else
            {
                ConsoleInputStream__redraw_line(&line, cursor, echo);
            }
        }
    }
}

static Optional(char) ConsoleInputStream__read_char(void)
{
    char target = '\0';
    if (console_read(&target, 1) != 1)
    {
        return None(char);
    }

    return Some(char, target);
}

static Optional(BString) ConsoleInputStream__read_escape_sequence(void)
{
    BString sequence = empty_bstr();
    match_optional(
        ConsoleInputStream__read_char(),
        some(first)
        {
            BString__push_char(&sequence, first);
            ;
        },
        none()
        {
            return None(BString);
            ;
        }
    );

    match_optional(
        ConsoleInputStream__read_char(),
        some(second)
        {
            BString__push_char(&sequence, second);
            ;
        },
        none()
        {
            return None(BString);
            ;
        }
    );

    return Some(BString, sequence);
}

static void ConsoleInputStream__redraw_line(ConstBStringPtr line, StringPos cursor, bool echo)
{
    if (!echo)
    {
        return;
    }

    IO__print(&SHELL_REDRAW_PREFIX);
    IO__print(line);

    StringPos move_left = BString__length(line) - cursor;
    if (move_left > 0)
    {
        IO__printf(&bstr("\033[%iD"), (int)move_left);
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

    match_result(
        ShellHistory__push(&self->history, *line),
        success_do_nothing(),
        error(code, message)
        {
            IO__printf(&bstr("shell: failed to store history: %B (%i)\n"), &message, (int)code);
            return;
        }
    );
}

static void Shell__handle_result(ConstShellPtr self, ShellResult result)
{
    switch (result)
    {
    case EXIT:
        IO__print(&bstr("Shell exiting\n"));
        break;
    case COMMAND_NOT_FOUND:
        IO__print(&bstr("Command not found\n"));
        break;
    case COMMAND_FAILED:
        IO__print(&bstr("Command failed\n"));
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
    IO__print(&SHELL_PROMPT);
    BString line = InputStream__read_line(self->input, true);
    IO__print(&bstr("\n"));

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
