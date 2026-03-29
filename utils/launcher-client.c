#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// Exit codes
#define EXIT_SUCCESS        0
#define EXIT_ARGS_ERROR     1
#define EXIT_CONNECT_ERROR  2
#define EXIT_SEND_ERROR     3
#define EXIT_RECV_ERROR     4

// Default values
#define DEFAULT_TIMEOUT_SEC 2
#define DEFAULT_SERVER      "127.0.0.1:8081"
#define BUFFER_SIZE         4096

void print_usage(const char* program_name) {
    printf("Usage: %s [--srv=IP:PORT] --command=COMMAND [--command-arg=ARG] [--timeoutsec=N]\n\n", program_name);
    printf("Options:\n");
    printf("  --srv=IP:PORT           Server address (default: 127.0.0.1:8081)\n");
    printf("  --command=COMMAND       Command to send\n");
    printf("  --command-arg=ARG       Optional argument for command\n");
    printf("  --timeoutsec=N          Connection timeout in seconds (default: %d)\n", DEFAULT_TIMEOUT_SEC);
    printf("  -h, --help              Show this help message\n\n");
    printf("Supported commands:\n");
    printf("  list-apps                          List all enabled applications\n");
    printf("  start-app (--command-arg=<id>)     Start application by ID\n");
    printf("  stop-app                           Stop currently running application\n");
    printf("  get-running-app                    Get currently running app ID or 'none'\n");
    printf("  reload-config                      Reload JSON configuration from file\n");
    printf("  set-button-enabled (--command-arg=\"<id> <true|false>\")  Enable/disable button\n");
    printf("  get-button-status (--command-arg=<id>)  Get button enabled/disabled status\n");
    printf("  list-all-buttons                   List all buttons with their status\n");
    printf("\nExamples:\n");
    printf("  %s --command=get-running-app\n", program_name);
    printf("  %s --command=list-apps\n", program_name);
    printf("  %s --command=stop-app\n", program_name);
    printf("  %s --command=start-app --command-arg=gallery\n", program_name);
    printf("  %s --srv=192.168.1.100:8081 --command=get-running-app\n", program_name);
    printf("  %s --srv=127.0.0.1:8082 --command=pattern --command-arg=red\n", program_name);
    printf("\nOutput:\n");
    printf("  Prints server response to stdout\n");
    printf("  Exit code 0 on success, non-zero on error\n");
}

int parse_server_address(const char* srv_str, char* host, int host_len, int* port) {
    // Find the colon separating host and port
    const char* colon = strchr(srv_str, ':');
    if (!colon) {
        fprintf(stderr, "Error: Invalid server address format. Expected IP:PORT\n");
        return -1;
    }

    // Extract host
    int host_part_len = colon - srv_str;
    if (host_part_len >= host_len) {
        fprintf(stderr, "Error: Host address too long\n");
        return -1;
    }
    strncpy(host, srv_str, host_part_len);
    host[host_part_len] = '\0';

    // Extract port
    *port = atoi(colon + 1);
    if (*port <= 0 || *port > 65535) {
        fprintf(stderr, "Error: Invalid port number: %s\n", colon + 1);
        return -1;
    }

    return 0;
}

int connect_to_server(const char* host, int port, int timeout_sec) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct timeval timeout;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error: Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    // Set socket timeout
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        fprintf(stderr, "Error: Failed to set receive timeout: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        fprintf(stderr, "Error: Failed to set send timeout: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Setup server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid IP address: %s\n", host);
        close(sockfd);
        return -1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error: Failed to connect to %s:%d: %s\n", host, port, strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int send_command(int sockfd, const char* command, const char* command_arg) {
    char buffer[BUFFER_SIZE];
    int len;

    // Build command string
    if (command_arg && strlen(command_arg) > 0) {
        len = snprintf(buffer, BUFFER_SIZE, "%s %s\n", command, command_arg);
    } else {
        len = snprintf(buffer, BUFFER_SIZE, "%s\n", command);
    }

    if (len >= BUFFER_SIZE) {
        fprintf(stderr, "Error: Command too long\n");
        return -1;
    }

    // Send command
    ssize_t sent = send(sockfd, buffer, len, 0);
    if (sent < 0) {
        fprintf(stderr, "Error: Failed to send command: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int receive_response(int sockfd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int total_received = 0;

    // Receive response
    while ((bytes_received = recv(sockfd, buffer + total_received,
                                  BUFFER_SIZE - total_received - 1, 0)) > 0) {
        total_received += bytes_received;

        // Check if we've received a complete response (ends with newline)
        if (total_received > 0 && buffer[total_received - 1] == '\n') {
            break;
        }

        // Prevent buffer overflow
        if (total_received >= BUFFER_SIZE - 1) {
            break;
        }
    }

    if (bytes_received < 0) {
        // Check if it's a timeout (which is normal for some commands)
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Timeout - check if we received any data
            if (total_received > 0) {
                buffer[total_received] = '\0';
                printf("%s", buffer);
                // Add newline if response doesn't have one
                if (buffer[total_received - 1] != '\n') {
                    printf("\n");
                }
                return 0;
            } else {
                fprintf(stderr, "Error: Timeout waiting for response\n");
                return -1;
            }
        }
        fprintf(stderr, "Error: Failed to receive response: %s\n", strerror(errno));
        return -1;
    }

    if (total_received == 0) {
        fprintf(stderr, "Error: Server closed connection without response\n");
        return -1;
    }

    // Null-terminate and print response
    buffer[total_received] = '\0';
    printf("%s", buffer);

    // Add newline if response doesn't have one
    if (buffer[total_received - 1] != '\n') {
        printf("\n");
    }

    return 0;
}

int main(int argc, char* argv[]) {
    char* srv_str = NULL;
    char* command = NULL;
    char* command_arg = NULL;
    int timeout_sec = DEFAULT_TIMEOUT_SEC;
    char host[256];
    int port = 0;
    int sockfd = -1;
    int ret = EXIT_SUCCESS;

    // Define long options
    static struct option long_options[] = {
        {"srv",         required_argument, 0, 's'},
        {"command",     required_argument, 0, 'c'},
        {"command-arg", required_argument, 0, 'a'},
        {"timeoutsec",  required_argument, 0, 't'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    // Parse command line options
    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hs:c:a:t:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                srv_str = optarg;
                break;
            case 'c':
                command = optarg;
                break;
            case 'a':
                command_arg = optarg;
                break;
            case 't':
                timeout_sec = atoi(optarg);
                if (timeout_sec <= 0) {
                    fprintf(stderr, "Error: Timeout must be > 0\n");
                    return EXIT_ARGS_ERROR;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_ARGS_ERROR;
        }
    }

    // Apply default server if not specified
    if (!srv_str) {
        srv_str = DEFAULT_SERVER;
    }

    // Validate required arguments
    if (!command) {
        fprintf(stderr, "Error: --command is required\n");
        print_usage(argv[0]);
        return EXIT_ARGS_ERROR;
    }

    // Parse server address
    if (parse_server_address(srv_str, host, sizeof(host), &port) < 0) {
        return EXIT_ARGS_ERROR;
    }

    // Connect to server
    sockfd = connect_to_server(host, port, timeout_sec);
    if (sockfd < 0) {
        return EXIT_CONNECT_ERROR;
    }

    // Send command
    if (send_command(sockfd, command, command_arg) < 0) {
        ret = EXIT_SEND_ERROR;
        goto cleanup;
    }

    // Receive and print response
    if (receive_response(sockfd) < 0) {
        ret = EXIT_RECV_ERROR;
        goto cleanup;
    }

cleanup:
    if (sockfd >= 0) {
        close(sockfd);
    }

    return ret;
}
