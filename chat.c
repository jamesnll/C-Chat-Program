// Data Types and Limits
#include <inttypes.h>
#include <stdint.h>

// Error Handling
#include <errno.h>

// Network Programming
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>

// Signal Handling
#include <signal.h>

// Standard Library
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Macros
#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BASE_TEN 10
// #define LINE_LENGTH 1024

// ----- Function Headers -----

// Argument Parsing
static void      parse_arguments(int argc, char *argv[], bool *connect, bool *listen, char **ip_address, char **port);
static void      handle_arguments(const char *binary_name, bool connect, bool listen, const char *ip_address, const char *port_str, in_port_t *port);
static in_port_t parse_in_port_t(const char *binary_name, const char *port_str);

// Error Handling
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);

int main(int argc, char *argv[])
{
    bool      connect_arg;
    bool      listen_arg;
    char     *ip_address;
    char     *port_str;
    in_port_t port;

    connect_arg = false;
    listen_arg  = false;
    ip_address  = NULL;
    port_str    = NULL;

    parse_arguments(argc, argv, &connect_arg, &listen_arg, &ip_address, &port_str);
    handle_arguments(argv[0], connect_arg, listen_arg, ip_address, port_str, &port);

    printf("Connect: %d\n", connect_arg);
    printf("Listen: %d\n", listen_arg);
    return EXIT_SUCCESS;
}

// ----- Function Definitions -----

// Argument Parsing Functions
static void parse_arguments(const int argc, char *argv[], bool *connect, bool *listen, char **ip_address, char **port)
{
    int opt;
    opterr = 0;

    // Option parsing
    while((opt = getopt(argc, argv, "ach:")) != -1)
    {
        switch(opt)
        {
            case 'a':    // Listen argument
            {
                if(*connect)    // Checks if connect was already set to true
                {
                    usage(argv[0], EXIT_FAILURE, "Arguments a and c are mutually exclusive");
                }
                *listen = true;
                break;
            }
            case 'c':    // Connect argument
            {
                if(*listen)    // Checks if listen was already set to true
                {
                    usage(argv[0], EXIT_FAILURE, "Arguments a and c are mutually exclusive");
                }
                *connect = true;
                break;
            }
            case 'h':    // Help argument
            {
                usage(argv[0], EXIT_SUCCESS, NULL);
            }
            case '?':    // Unknown argument
            {
                char message[UNKNOWN_OPTION_MESSAGE_LEN];

                snprintf(message, sizeof(message), "Unknown option '-%c'.", optopt);
                usage(argv[0], EXIT_FAILURE, message);
            }
            default:
            {
                usage(argv[0], EXIT_FAILURE, NULL);
            }
        }
    }

    if(optind >= argc)    // Check for sufficient args
    {
        usage(argv[0], EXIT_FAILURE, "The ip address and port are required.");
    }

    if(optind + 1 >= argc)    // Check for port arg
    {
        usage(argv[0], EXIT_FAILURE, "The port is required.");
    }

    if(optind < argc - 2)    // Check for extra args
    {
        usage(argv[0], EXIT_FAILURE, "Error: Too many arguments.");
    }

    *ip_address = argv[optind];
    *port       = argv[optind + 1];
}

static void handle_arguments(const char *binary_name, const bool connect, const bool listen, const char *ip_address, const char *port_str, in_port_t *port)
{
    if(ip_address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The ip address is required.");
    }

    if(port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    if(!connect && !listen)
    {
        usage(binary_name, EXIT_FAILURE, "Argument -a or -c are required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

static in_port_t parse_in_port_t(const char *binary_name, const char *port_str)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(port_str, &endptr, BASE_TEN);

    // Check for errno was signalled
    if(errno != 0)
    {
        perror("Error parsing in_port_t.");
        exit(EXIT_FAILURE);
    }

    // Check for any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is within valid range of in_port_t
    if(parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

// Error Handling Functions

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] [-a] [-c] <ip address> <port>\n", program_name);
    fputs("Options:\n", stderr);
    fputs(" -h Display this help message\n", stderr);
    exit(exit_code);
}
