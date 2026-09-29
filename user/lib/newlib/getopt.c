#include <stdio.h>
#include <getopt.h>
#include <string.h>
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

int getopt_long(int argc, char * const argv[], const char * options, const struct option * long_options,
                int * option_index)
{
    if (optind >= argc || argv[optind] == NULL || strncmp(argv[optind], "--", 2) != 0 || argv[optind][2] == '\0')
    {
        return getopt(argc, argv, options);
    }
    const char * name = argv[optind] + 2;
    const char * equals = strchr(name, '=');
    size_t name_length = equals == NULL ? strlen(name) : (size_t)(equals - name);
    for (int index = 0; long_options[index].name != NULL; index++)
    {
        if (strlen(long_options[index].name) != name_length ||
            strncmp(long_options[index].name, name, name_length) != 0)
        {
            continue;
        }
        optind++;
        optarg = equals == NULL ? NULL : (char *)equals + 1;
        if (long_options[index].has_arg == required_argument && optarg == NULL)
        {
            if (optind >= argc)
            {
                return '?';
            }
            optarg = argv[optind++];
        }
        if (long_options[index].has_arg == no_argument && optarg != NULL)
        {
            return '?';
        }
        if (option_index != NULL)
        {
            *option_index = index;
        }
        if (long_options[index].flag != NULL)
        {
            *long_options[index].flag = long_options[index].val;
            return 0;
        }
        return long_options[index].val;
    }
    optind++;
    return '?';
}
