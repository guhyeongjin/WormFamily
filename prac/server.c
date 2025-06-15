#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>

#define PORT 443
#define BUFFER_SIZE 2048
#define MAX_CLIENTS 100000
#define UNIQUE_ID_LENGTH 8
#define BASE_DIR "/var/www/html/worldmap/client"

// 현재시간과 랜덤값 조합하여 고유 ID 생성
void generate_unique_id(char *id_buffer) {
    long long timestamp = (long long)time(NULL);
    long long random_val = rand() % 1000000;
    snprintf(id_buffer, UNIQUE_ID_LENGTH + 1, "%04llx%04llx", timestamp % 0xFFFF, random_val % 0xFFFF);
}

// 클라이언트 소켓 관리
void remove_client(int *client_sockets, int *num_clients, int client_fd_to_remove) {
    for (int i = 0; i < *num_clients; i++) {
        if (client_sockets[i] == client_fd_to_remove) {
            close(client_sockets[i]);
            for (int j = i; j < *num_clients - 1; j++) {
                client_sockets[j] = client_sockets[j+1];
            }
            (*num_clients)--;
            printf("\n -------- \n Client disconnected: %d\n --------- \n", *num_clients);
            return;
        }
    }
}

int main() {
    int listen_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size;
    char buffer[BUFFER_SIZE];

    // 소켓 초기화
    fd_set readfds;
    int client_sockets[MAX_CLIENTS];
    int num_clients = 0;
    int max_sd, activity;

    srand(time(NULL));  // 랜덤 시드 초기화

    // 소켓 생성
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("\n\n!!!!!!!!!!!!\n Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // SO_REUSEADDR 옵션 설정 포트 재사용 허용
    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("\n\n!!!!!!!!!!!!\n Set socket options failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT); //443

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("\n\n!!!!!!!!!!!!\n Bind failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, 100) < 0) {
        perror("\n\n!!!!!!!!!!!!\n Listen failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    printf("\n\n!!!!!!!!!!!!\n Server is listening on port %d\n", PORT);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = 0; // 초기화
    }

    while (1) {
        // 소켓 초기화
        FD_ZERO(&readfds);
        FD_SET(listen_sock, &readfds);
        max_sd = listen_sock;

        for (int i = 0; i < num_clients; i++) {
            int sd = client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }
        activity = select(max_sd +1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("\n\n!!!!!!!!!!!!\n Select error");
        }

        if (FD_ISSET(listen_sock, &readfds)) {
            client_addr_size = sizeof(client_addr);
            client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_size);
            if (client_sock < 0) {
                perror("\n\n!!!!!!!!!!!!\n Accept failed");
                exit(EXIT_FAILURE);
            }

            printf("\n\n!!!!!!!!!!!!\n New connection: \n socket fd is %d,\n ip is : %s,\n port: %d\n", client_sock, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            if (num_clients < MAX_CLIENTS) {
                client_sockets[num_clients++] = client_sock;
                printf("\n -------- \n Client connected: %d\n --------- \n", num_clients);
            } else {
                printf("\n\n!!!!!!!!!!!!\n Max clients reached, rejecting new connection\n");
                close(client_sock);
            }
        }

        for (int i = 0; i < num_clients; i++) {
            int sd = client_sockets[i];

            if (FD_ISSET(sd, &readfds)) {
                ssize_t valread = recv(sd, buffer, BUFFER_SIZE, 0);
                if (valread == 0) {
                    printf(" \n\n!!!!!!!!!!!!\n Client disconnected: %d\n", sd);
                    remove_client(client_sockets, &num_clients, sd);
                }
                else if (valread < 0) {
                    perror(" \n\n!!!!!!!!!!!!\n Receive failed");
                    remove_client(client_sockets, &num_clients, sd);
                }
                else {
                    buffer[valread] = '\0';
                    printf("\n\n!!!!!!!!!!!!\n Received froem client %d: %s\n", sd, buffer);

                    char unique_id[UNIQUE_ID_LENGTH + 1] = {0};
                    char *ip_addr_str = NULL;
                    char *mac_addr_str = NULL;
                    char *token;

                    char *temp_buffer = strdup(buffer);
                    if (temp_buffer == NULL) {
                        perror("\n\n!!!!!!!!!!!!\n Strdump failed");
                        remove_client(client_sockets, &num_clients, sd);
                        continue;
                    }

                    token = strtok(temp_buffer, ":");
                    if (token != NULL) {
                        if (strlen(token) == )
                    }
                }
            }
        }

    }


}