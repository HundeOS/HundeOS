#include <stdio.h>
#include <unistd.h>

char * optarg;
int opterr = 1;
int optind = 1;
int optopt;

static int option_position = 1;

static const char * Getopt__find_option(const char * options, char option)
{
    while (*options != '\0')
    {
        if (*options == option)
        {
            return options;
        }
        options++;
    }
    return NULL;
}

int getopt(int argc, char * const argv[], const char * options)
{
    optarg = NULL;
    if (optind >= argc || argv[optind] == NULL || argv[optind][0] != '-' || argv[optind][1] == '\0')
    {
        return -1;
    }
    if (argv[optind][1] == '-' && argv[optind][2] == '\0')
    {
        optind++;
        option_position = 1;
        return -1;
    }

    char option = argv[optind][option_position++];
    optopt = option;
    const char * definition = Getopt__find_option(options, option);
    if (argv[optind][option_position] == '\0')
    {
        optind++;
        option_position = 1;
    }

    if (definition == NULL || option == ':')
    {
        return '?';
    }
    if (definition[1] != ':')
    {
        return option;
    }

    if (option_position != 1)
    {
        optarg = &argv[optind][option_position];
        optind++;
        option_position = 1;
        return option;
    }
    if (optind < argc)
    {
        optarg = argv[optind++];
        return option;
    }
    return options[0] == ':' ? ':' : '?';
}
