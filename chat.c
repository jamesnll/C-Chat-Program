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

// Signal Handling
#include <signal.h>

// Standard Library
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Macros
#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BASE_TEN 10
#define LINE_LENGTH 1024

// ----- Function Headers -----

// Argument Parsing
static void      parse_arguments(int argc, char *argv[], bool *connect, bool *listen, char **ip_address, char **port);
static void      handle_arguments(const char *binary_name, bool connect, bool listen, const char *ip_address, const char *port_str, in_port_t *port);
static in_port_t parse_in_port_t(const char *binary_name, const char *port_str);

// Error Handling
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);

// Network Handling
static void convert_address(const char *address, struct sockaddr_storage *addr);
static int  socket_create(int domain, int type, int protocol);
static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void start_listening(int server_fd, int backlog);
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static int  socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);
static void socket_close(int sockfd);
static void write_to_socket(int sockfd, const char *message);
static void read_from_socket(int sockfd);

// Network Helper Functions
void host_connection(int sockfd, struct sockaddr_storage *addr, in_port_t port);

// Signal Handling Functions
static void setup_signal_handler(void);
static void sigtstp_handler(int signum);

// Thread Functions
static void *write_message(void *arg);
static void *read_message(void *arg);

static volatile sig_atomic_t sigtstp_flag = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// ----- Main Function -----

int main(int argc, char *argv[])
{
    bool                    connect_arg;
    bool                    listen_arg;
    char                   *ip_address;
    char                   *port_str;
    in_port_t               port;
    int                     host_sockfd;
    int                     receiver_sockfd;
    struct sockaddr_storage addr;

    // Client socket variables
    int                     client_sockfd;
    struct sockaddr_storage client_addr;
    socklen_t               client_addr_len;

    // Threads
    pthread_t write_message_thread;    // Gets input from stdin and writes to the network
    pthread_t read_message_thread;     // Reads messages from network
    int       read_thread_result;
    int       write_thread_result;

    connect_arg = false;
    listen_arg  = false;
    ip_address  = NULL;
    port_str    = NULL;

    client_sockfd = 0;

    parse_arguments(argc, argv, &connect_arg, &listen_arg, &ip_address, &port_str);
    handle_arguments(argv[0], connect_arg, listen_arg, ip_address, port_str, &port);
    convert_address(ip_address, &addr);

    host_sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);

    if(connect_arg)
    {
        socket_connect(host_sockfd, &addr, port);
    }

    if(listen_arg)
    {
        host_connection(host_sockfd, &addr, port);    // Call setsockopt, bind, listen

        // Handle incoming client connections
        while(client_sockfd == 0)
        {
            client_addr_len = sizeof(client_addr);
            client_sockfd   = socket_accept_connection(host_sockfd, &client_addr, &client_addr_len);
            if(client_sockfd == -1)
            {
                perror("accept");
                exit(EXIT_FAILURE);
            }
        }
    }

    setup_signal_handler();

    // Sets the receiving end sockfd to either client_sockfd or host_sockfd
    // If -a is set, receiver is client, If -c is set, receiver is host
    receiver_sockfd = listen_arg ? client_sockfd : host_sockfd;

    read_thread_result  = pthread_create(&read_message_thread, NULL, read_message, (void *)&receiver_sockfd);
    write_thread_result = pthread_create(&write_message_thread, NULL, write_message, (void *)&receiver_sockfd);

    if(write_thread_result != 0)
    {
        perror("Write thread creation failed");
        return EXIT_FAILURE;
    }

    if(read_thread_result != 0)
    {
        perror("Read thread creation failed");
        return EXIT_FAILURE;
    }

    if(sigtstp_flag)
    {
        socket_close(client_sockfd);
        socket_close(host_sockfd);
        return EXIT_SUCCESS;
    }

    pthread_join(read_message_thread, NULL);
    pthread_join(write_message_thread, NULL);
    printf("Read finished\n");

    socket_close(client_sockfd);
    socket_close(host_sockfd);
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

// Network Handling Functions

/**
 * Converts the address from a human-readable string into a binary representation.
 * @param address string IP address in human-readable format (e.g., "192.168.0.1")
 * @param addr    pointer to the struct sockaddr_storage where the binary representation will be stored
 */
static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    // Converts the str address to binary address and checks for IPv4 or IPv6
    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        // IPv4 address
        addr->ss_family = AF_INET;
        printf("IPv4 found\n");
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        // IPv6 address
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

/**
 * Creates a socket with the specified domain, type, and protocol.
 * @param domain   the communication domain, e.g., AF_INET for IPv4 or AF_INET6 for IPv6
 * @param type     the socket type, e.g., SOCK_STREAM for a TCP socket or SOCK_DGRAM for a UDP socket
 * @param protocol the specific protocol to be used, often set to 0 for the default protocol
 * @return         the file descriptor for the created socket, or -1 on error
 */
static int socket_create(int domain, int type, int protocol)
{
    int sockfd;

    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        fprintf(stderr, "Socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

/**
 * Binds a socket to the specified address and port.
 * @param sockfd    the socket file descriptor
 * @param addr      a pointer to the struct sockaddr_storage containing the address
 * @param port      the port number to bind the socket
 */
static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];    // Array to store human-readable IP address for either IPv4 or IPv6
    socklen_t addr_len;                      // Variable to store the length of the addr struct
    void     *vaddr;                         // Pointer to actual (binary) IP address within addr struct
    in_port_t net_port;                      // Stores network byte order representation of port number

    // Convert port number to network byte order (big endian)
    net_port = htons(port);

    // Handle IPv4
    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        addr_len            = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr               = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    // Handle IPv6
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        addr_len             = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr                = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr, "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    // Converts binary IP address to a human-readable string and stores it in addr_str
    if(inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Binding to %s:%u\n", addr_str, port);

    // Bind socket to port
    if(bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

/**
 * Starts listening on a server socket for incoming connections with a specified backlog.
 * @param server_fd the file descriptor of the server socket to start listening on
 * @param backlog   the maximum number of pending connections that can be queued up
 */
static void start_listening(int server_fd, int backlog)
{
    if(listen(server_fd, backlog) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

/**
 * Accepts an incoming connection on the server socket.
 * @param server_fd         the file descriptor of the server socket
 * @param client_addr       a pointer to a struct sockaddr_storage for storing client address information
 * @param client_addr_len   a pointer to the length of the client address structure
 * @return                 the file descriptor for the accepted connection, or -1 on error
 */
static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
{
    int  client_fd;
    char client_host[NI_MAXHOST];       // Array to store the hostname of the client
    char client_service[NI_MAXSERV];    // Array to store the port information of the client

    errno     = 0;
    client_fd = accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

    if(client_fd == -1)
    {
        if(errno != EINTR)
        {
            perror("accept failed");
        }

        return -1;
    }

    // Attempts to successfully convert the address information
    if(getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        printf("Accepted a new connection from %s:%s\n", client_host, client_service);
    }
    else
    {
        printf("Unable to get client information\n");
    }

    return client_fd;
}

/**
 * Establishes a network connection to a remote address and port using a given socket.
 * @param sockfd the file descriptor of the socket for the connection
 * @param addr   a pointer to a struct sockaddr_storage containing the remote address
 * @param port   the port to which the connection should be established
 */
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];    // Array to store human-readable IP address for either IPv4 or IPv6
    in_port_t net_port;                      // Stores network byte order representation of port number
    socklen_t addr_len;                      // Stores the address length

    // Converts binary IP address (IPv4 or IPv6) to a human-readable string, stores it in addr_str, and handles errors
    if(inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to %s:%u\n", addr_str, port);
    net_port = htons(port);    // Convert port number to network byte order (big endian)

    // Handle IPv4
    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
        addr_len            = sizeof(struct sockaddr_in);
    }
    // Handle IPv6
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
        addr_len             = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if(connect(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
}

/**
 * Closes a socket with the specified file descriptor.
 * @param sockfd the file descriptor of the socket to be closed
 */
static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("error closing socket");
        exit(EXIT_FAILURE);
    }
}

// Network Helper Functions

/**
 * Helper function that hosts the connection between programs.
 * @param sockfd the socket file descriptor
 * @param addr      a pointer to the struct sockaddr_storage containing the address
 * @param port      the port number to host the connection
 */
void host_connection(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    int enable = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == -1)
    {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    socket_bind(sockfd, addr, port);
    start_listening(sockfd, SOMAXCONN);
}

// Signal Handling Functions

/**
 * Sets up a signal handler for the application.
 */
static void setup_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

// Disable specific clang compiler warning related to macro expansion.
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif

    // Set the signal handler function for SIGTSTP (Ctrl+Z) to 'sigtstp_handler'.
    sa.sa_handler = sigtstp_handler;

// Restore the previous Clang compiler warning settings.
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);    // Clear the sa_mask, which is used to block signals during the signal handler execution.
    sa.sa_flags = 0;             // Set sa_flags to 0, indicating no special flags for signal handling.

    // Register the signal handler configuration ('sa') for the SIGINT signal.
    if(sigaction(SIGTSTP, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/**
 * Signal handler function for the SIGTSTP (Ctrl+Z) signal.
 * @param signum the signal number, typically SIGTSTP (2) in this context
 */
static void sigtstp_handler(int signum)
{
    sigtstp_flag = 1;
}

#pragma GCC diagnostic pop

static void *write_message(void *arg)
{
    int sockfd = *((int *)arg);

    while(!sigtstp_flag)
    {
        char input[LINE_LENGTH];
        fgets(input, sizeof(input), stdin);

        write_to_socket(sockfd, input);
    }

    pthread_exit(NULL);
}

static void *read_message(void *arg)
{
    int sockfd = *((int *)arg);

    while(!sigtstp_flag)
    {
        read_from_socket(sockfd);
    }

    pthread_exit(NULL);
}

/**
 * Writes a command string to a socket.
 * @param sockfd   the file descriptor of the socket to write to
 * @param command  the command string to write to the socket
 */
static void write_to_socket(int sockfd, const char *message)
{
    size_t   message_len;
    uint16_t size;

    message_len = strlen(message);
    size        = (uint16_t)message_len;

    write(sockfd, &size, sizeof(uint16_t));    // Write the size of the command
    write(sockfd, message, message_len);       // Write the command string
}

/**
 * Reads input from the network socket
 * @param client_sockfd   the file descriptor for the connected client socket
 * @param client_addr     a pointer to a struct sockaddr_storage containing client address information
 */
static void read_from_socket(int sockfd)
{
    ssize_t  bytes_read;
    uint16_t size;
    char     buffer[LINE_LENGTH];

    bytes_read = read(sockfd, &size, sizeof(uint16_t));

    if(bytes_read == 0)    // Check if connection is closed
    {
        exit(EXIT_SUCCESS);
    }

    bytes_read = read(sockfd, buffer, size);

    if(bytes_read == 0)    // Check if connection is closed
    {
        exit(EXIT_SUCCESS);
    }

    buffer[(int)bytes_read] = '\0';

    if(write(STDOUT_FILENO, buffer, strlen(buffer)) == -1)
    {
        perror("write");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
}

/*
 * Current issues:
 * Getting input with I/O redirection results in an infinite loop
 */
