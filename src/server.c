#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include "commands.h"
#include "response.h"
#include "resp.h"
#include "memory.h"
#include "time_utils.h"
#include "config.h"
#include <fcntl.h>
#include <sys/epoll.h>
#include <ctype.h>
#include <time.h>


#define BUFFER_SIZE 1024
#define MAX_EVENTS 1000
#define MAX_ARGS 10
#define CLEANUP_INTERVAL_SECONDS 60 
#define DEFAULT_DIR "./"          // Default directory
#define DEFAULT_DBFILENAME "dump.rdb" // Default RDB filename


/**
 * Sets a file descriptor to non-blocking mode.
 * Returns 0 on success, -1 on failure.
 */
int set_nonblocking(int fd) {
    int flags, s;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(fd, F_SETFL, flags);
    if (s == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }

    return 0;
}

/**
 * Handles a command by parsing input and dispatching to the appropriate handler.
 * Returns the response string.
 */
/**
 * Handles a command by parsing input and dispatching to the appropriate handler.
 * Returns the response string.
 */
Response handle_command(const char* input) {
    int argc = 0;
    char* argv[MAX_ARGS] = {0};
    parse_resp(input, &argc, argv);

    Response res;
    if (argc > 0) {
        // Convert command to uppercase for case-insensitive comparison
        for(int i = 0; argv[0][i]; i++){
            argv[0][i] = toupper((unsigned char)argv[0][i]);
        }

        if (strcmp(argv[0], "PING") == 0) {
            res = handle_ping();
        } else if (strcmp(argv[0], "ECHO") == 0 && argc > 1) {
            res = handle_echo(argv[1]);
        } else if (strcmp(argv[0], "SET") == 0 && argc > 2) {
            res = handle_set(argc, argv);
        } else if (strcmp(argv[0], "GET") == 0 && argc > 1) {
            res = handle_get(argv[1]);
        } else if (strcmp(argv[0], "CONFIG") == 0 && argc >= 3) { 
            res = handle_config_get(argc, argv);
        } else if (strcmp(argv[0], "KEYS") == 0 && argc >= 2) {
            res = handle_keys(argc, argv);
        } else {
            res.response = "-ERR unknown command\r\n";
            res.should_free = 0;
        }
    } else {
        res.response = "-ERR unknown command\r\n";
        res.should_free = 0;
    }

    // Free allocated memory for argv
    for(int i = 0; i < argc; i++) {
        free(argv[i]);
    }

    return res;
}

int main(int argc, char* argv[]) {
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    int server_fd, epoll_fd;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_addr_len;
    struct epoll_event ev, events[MAX_EVENTS];


	// Initialize default configurations
    config_dir = (char *)strdup(DEFAULT_DIR);
	config_dbfilename = (char *)strdup(DEFAULT_DBFILENAME);

	// Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            free(config_dir); // Free the default
            config_dir = strdup(argv[++i]);
        } else if (strcmp(argv[i], "--dbfilename") == 0 && i + 1 < argc) {
            free(config_dbfilename); // Free the default
            config_dbfilename = strdup(argv[++i]);
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s --dir <directory> --dbfilename <filename>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

	init_store();

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    // Allow address reuse
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("SO_REUSEADDR failed: %s \n", strerror(errno));
        close(server_fd);
        return 1;
    }

    // Initialize server address structure
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(6379);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind socket to the specified address and port
    if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
        printf("Bind failed: %s \n", strerror(errno));
        close(server_fd);
        return 1;
    }

    // Start listening for incoming connections
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        printf("Listen failed: %s \n", strerror(errno));
        close(server_fd);
        return 1;
    }

    printf("Waiting for clients to connect...\n");

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }

    // Make server socket non-blocking
    if (set_nonblocking(server_fd) == -1) {
        close(server_fd);
        close(epoll_fd);
        return 1;
    }

    // Add server_fd to epoll instance
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1){
        perror("epoll_ctl: server_fd");
        close(server_fd);
        close(epoll_fd);
        return 1;
    }

	time_t last_cleanup = time(NULL);
    const int cleanup_interval_seconds = CLEANUP_INTERVAL_SECONDS;

    // Event loop
    while (1) {
        // Wait for events with a timeout to allow periodic cleanup
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // 1 second timeout
        if (nfds == -1) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            perror("epoll_wait");
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
                // Handle all incoming connections
                while (1) {
                    client_addr_len = sizeof(client_addr);
                    int client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                    if (client_socket == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // No more incoming connections
                            break;
                        } else {
                            perror("accept");
                            break;
                        }
                    }

                    // Make client socket non-blocking
                    if (set_nonblocking(client_socket) == -1) {
                        close(client_socket);
                        continue;
                    }

                    // Add client socket to epoll instance
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = client_socket;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev) == -1){
                        perror("epoll_ctl: client_socket");
                        close(client_socket);
                        continue;
                    }

                    printf("New client connected (fd: %d)\n", client_socket);
                }
            } else {
                // Handle data from client
                int client_socket = events[n].data.fd;
                int done = 0;

                while (1) {
                    char buffer[BUFFER_SIZE] = {0};
                    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);

                    if (bytes_read == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // All data has been read
                            break;
                        }
                        perror("read");
                        done = 1;
                        break;
                    } else if (bytes_read == 0) {
                        // Client disconnected
                        done = 1;
                        break;
                    }

                    // Null-terminate the received data
                    buffer[bytes_read] = '\0';
                    printf("Received from fd %d: %s\n", client_socket, buffer);

                    // Handle the command and get the response
                    Response response = handle_command(buffer);
                    ssize_t bytes_sent = send(client_socket, response.response, strlen(response.response), 0);
                    if (bytes_sent == -1) {
                        perror("send");
                        done = 1;
                        break;
                    }

                    // Free the response if it was dynamically allocated
                    if (response.should_free) {
                        free((void*)response.response);
                    }
                }

                if (done) {
                    printf("Closing connection with fd %d\n", client_socket);
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL) == -1) {
                        perror("epoll_ctl: EPOLL_CTL_DEL");
                    }
                    close(client_socket);
                }
            }
        }

        // Periodic cleanup of expired keys
        time_t current_time = time(NULL);
		
		
        if (difftime(current_time, last_cleanup) >= cleanup_interval_seconds) {
            // Iterate through keyValueStore and remove expired keys
            for(int i = 0; i < keyValueCount; ) {
                if (keyValueStore[i].expiry != 0 && current_time_millis() > keyValueStore[i].expiry) {
                    printf("Key '%s' has expired. Removing it.\n", keyValueStore[i].key);
                    // Key has expired. Remove it.
                    free(keyValueStore[i].key);
                    free(keyValueStore[i].value);
                    // Shift remaining keys
                    for(int j = i; j < keyValueCount - 1; j++) {
                        keyValueStore[j] = keyValueStore[j + 1];
                    }
                    keyValueCount--;
                    // Do not increment i to check the new key at this index
                } else {
                    i++;
                }
            }
            last_cleanup = current_time;
        }
    }

    // Cleanup
    close(server_fd);
    close(epoll_fd);
    return 0;
}
