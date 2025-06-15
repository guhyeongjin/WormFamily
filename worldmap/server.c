#include "./common_handlers.h" // 공통 헤더
#include "handlers/handler_0001.h"    // 각 핸들러 헤더
#include "handlers/handler_0002.h"
#include "handlers/handler_0003.h"
#include "handlers/handler_4444.h"

int main() {
    int tcp_listen_sock, udp_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size;
    char buffer[BUFFER_SIZE];

    fd_set readfds;
    int tcp_client_sockets[MAX_CLIENTS];
    int num_tcp_clients = 0;
    int max_sd, activity;

    srand(time(NULL));

    // --- Create TCP socket ---
    tcp_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_listen_sock == -1) {
        perror("TCP socket creation failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(tcp_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed for TCP");
        close(tcp_listen_sock);
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(tcp_listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("TCP bind failed");
        close(tcp_listen_sock);
        exit(EXIT_FAILURE);
    }
    if (listen(tcp_listen_sock, 100) < 0) {
        perror("TCP listen failed");
        close(tcp_listen_sock);
        exit(EXIT_FAILURE);
    }
    printf("TCP Server listening on port %d\n", PORT);

    // --- Create UDP socket ---
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock == -1) {
        perror("UDP socket creation failed");
        close(tcp_listen_sock);
        exit(EXIT_FAILURE);
    }
    if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed for UDP");
        close(udp_sock);
        close(tcp_listen_sock);
        exit(EXIT_FAILURE);
    }
    if (bind(udp_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("UDP bind failed");
        close(udp_sock);
        close(tcp_listen_sock);
        exit(EXIT_FAILURE);
    }
    printf("UDP Server listening on port %d\n", PORT);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        tcp_client_sockets[i] = 0;
    }

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(tcp_listen_sock, &readfds);
        FD_SET(udp_sock, &readfds);
        max_sd = (tcp_listen_sock > udp_sock) ? tcp_listen_sock : udp_sock;

        for (int i = 0; i < num_tcp_clients; i++) {
            int sd = tcp_client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            printf("select error\n");
        }

        // --- Handle incoming TCP connections ---
        if (FD_ISSET(tcp_listen_sock, &readfds)) {
            int new_socket;
            client_addr_size = sizeof(client_addr);
            new_socket = accept(tcp_listen_sock, (struct sockaddr *)&client_addr, &client_addr_size);
            if (new_socket < 0) {
                perror("TCP accept failed");
                continue;
            }
            printf("[TCP] New connection from %s:%d, socket fd is %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), new_socket);

            if (num_tcp_clients < MAX_CLIENTS) {
                tcp_client_sockets[num_tcp_clients++] = new_socket;
                printf("[TCP] Adding to active TCP clients list as %d\n", num_tcp_clients - 1);
            } else {
                printf("[TCP] Maximum TCP clients reached. Connection rejected.\n");
                close(new_socket);
            }
        }

        // --- Handle data from existing TCP clients ---
        for (int i = 0; i < num_tcp_clients; i++) {
            int sd = tcp_client_sockets[i];
            if (FD_ISSET(sd, &readfds)) {
                ssize_t valread = recv(sd, buffer, BUFFER_SIZE, 0);
                if (valread == 0) {
                    printf("[TCP] Client disconnected: socket %d\n", sd);
                    remove_tcp_client(tcp_client_sockets, &num_tcp_clients, sd);
                } else if (valread < 0) {
                    perror("[TCP] recv failed");
                    remove_tcp_client(tcp_client_sockets, &num_tcp_clients, sd);
                } else {
                    buffer[valread] = '\0';
                    printf("[TCP] Received from socket %d: %s\n", sd, buffer);

                    if (valread >= HEADER_LENGTH) {
                        char header[HEADER_LENGTH + 1];
                        strncpy(header, buffer, HEADER_LENGTH);
                        header[HEADER_LENGTH] = '\0';

                        int header_int = atoi(header);

                        switch (header_int) {
                            case 1:
                                handle_0001_request(sd, inet_ntoa(client_addr.sin_addr));
                                break;
                            case 2:
                                handle_0002_data(buffer, inet_ntoa(client_addr.sin_addr));
                                break;
                            case 4:
                                handle_4444_request(sd, buffer + HEADER_LENGTH, inet_ntoa(client_addr.sin_addr));
                                break;
                            default:
                                printf("[TCP] Unknown or unsupported header '%s' for TCP connection. Data: %s\n", header, buffer);
                                break;
                        }
                    } else {
                        printf("[TCP] Received malformed short packet from socket %d: %s\n", sd, buffer);
                    }
                }
            }
        }

        // --- Handle data from UDP socket ---
        if (FD_ISSET(udp_sock, &readfds)) {
            client_addr_size = sizeof(client_addr);
            ssize_t valread = recvfrom(udp_sock, buffer, BUFFER_SIZE, 0,
                                      (struct sockaddr *)&client_addr, &client_addr_size);
            if (valread < 0) {
                perror("UDP recvfrom failed");
            } else {
                buffer[valread] = '\0';
                printf("[UDP] Received from %s:%d: %s\n",
                       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), buffer);

                if (valread >= HEADER_LENGTH) {
                    char header[HEADER_LENGTH + 1];
                    strncpy(header, buffer, HEADER_LENGTH);
                    header[HEADER_LENGTH] = '\0';

                    int header_int = atoi(header);

                    switch (header_int) {
                        case 2:
                            handle_0002_data(buffer, inet_ntoa(client_addr.sin_addr));
                            break;
                        case 3:
                            handle_0003_data(buffer, inet_ntoa(client_addr.sin_addr));
                            break;
                        default:
                            handle_generic_data(header, buffer, inet_ntoa(client_addr.sin_addr));
                            break;
                    }
                } else {
                    printf("[UDP] Received malformed short packet from %s:%d: %s\n",
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), buffer);
                }
            }
        }
    }

    close(tcp_listen_sock);
    close(udp_sock);
    return 0;
}