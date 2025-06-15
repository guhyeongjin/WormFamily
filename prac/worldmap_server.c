#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 443
#define MAX_CLIENTS 10005 
#define BUFFER_SIZE 2048
#define UNIQUE_ID_LENGTH 8
#define HEADER_LENGTH 4
#define BASE_DIR "/var/www/html/worldmap/client"
#define UNIQUE_ID_FILE_NAME "client_id.txt" // Client-side file name

// 8자리 unique id 생성
void generate_unique_id(char *id_buffer) {
    long long timestamp = (long long)time(NULL);
    long long random_val = rand() % 1000000;
    snprintf(id_buffer, UNIQUE_ID_LENGTH + 1, "%04llX%04llX", timestamp % 0xFFFF, random_val % 0xFFFF);
}

// 클라이언트 제거 개선
void remove_tcp_client(int *client_sockets, int *num_clients, int client_fd_to_remove) {
    for (int i = 0; i < *num_clients; i++) {
        if (client_sockets[i] == client_fd_to_remove) {
            close(client_sockets[i]); 

            // 제거할 소켓을 배열의 마지막 소켓과 교환
            client_sockets[i] = client_sockets[*num_clients - 1];
            (*num_clients)--; // 클라이언트 수 감소

            printf("[TCP] Client disconnected. Current TCP clients: %d\n", *num_clients);
            return;
        }
    }
}

// 0001 unique id 요청 처리
void handle_0001_request(int client_sock, const char *client_ip_str) {
    char unique_id[UNIQUE_ID_LENGTH + 1] = {0};
    generate_unique_id(unique_id);

    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", BASE_DIR, unique_id);

    if (mkdir(dir_path, 0755) == -1) {
        if (errno != EEXIST) {
            perror("Failed to create client directory");
            return;
        }
    }
    printf("[0001] Created directory '%s' for new client.\n", dir_path);

    char response_buffer[BUFFER_SIZE];
    // Format: "0001<8-digit-ID>"
    snprintf(response_buffer, sizeof(response_buffer), "0001%s", unique_id);

    if (send(client_sock, response_buffer, strlen(response_buffer), 0) < 0) {
        perror("Failed to send unique ID via TCP");
    } else {
        printf("[0001] Sent unique ID '%s' to client %s.\n", unique_id, client_ip_str);
    }
}

// 0002 IP/MAC 정보 처리
void handle_0002_data(const char *data, const char *client_ip_str) {
    // Expected format: "0002<8-digit-ID><IP_ADDR>:<MAC_ADDR>"
    if (strlen(data) < HEADER_LENGTH + UNIQUE_ID_LENGTH) {
        fprintf(stderr, "[0002] Invalid data length: %s\n", data);
        return;
    }

    // 헤더 추출 (필요 없음?)
    char received_header[HEADER_LENGTH + 1];
    strncpy(received_header, data, HEADER_LENGTH);
    received_header[HEADER_LENGTH] = '\0';

    // 고유 id 추출
    char unique_id[UNIQUE_ID_LENGTH + 1];
    strncpy(unique_id, data + HEADER_LENGTH, UNIQUE_ID_LENGTH);
    unique_id[UNIQUE_ID_LENGTH] = '\0';

    const char *payload = data + HEADER_LENGTH + UNIQUE_ID_LENGTH; // "IP_ADDR:MAC_ADDR"

    printf("[0002] Received data for ID %s from %s: %s\n", unique_id, client_ip_str, payload);

    char ip_addr_str[20] = {0};
    char mac_addr_str[20] = {0};

    char *temp_payload = strdup(payload);
    if (temp_payload == NULL) {
        perror("strdup failed");
        return;
    }

    char *token = strtok(temp_payload, ":");
    if (token != NULL) {
        strncpy(ip_addr_str, token, sizeof(ip_addr_str) - 1);
        token = strtok(NULL, ":");
        if (token != NULL) {
            strncpy(mac_addr_str, token, sizeof(mac_addr_str) - 1);
        }
    }
    free(temp_payload);

    if (strlen(ip_addr_str) == 0 || strlen(mac_addr_str) == 0) {
        fprintf(stderr, "[0002] Malformed IP/MAC data: %s\n", payload);
        return;
    }

    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", BASE_DIR, unique_id);

    // 파일 존재 확인
    if (access(dir_path, F_OK) == -1) {
        printf("[0002] Directory for ID %s does not exist. Attempting to create.\n", unique_id);
        if (mkdir(dir_path, 0755) == -1) {
            perror("[0002] Failed to create client directory for data");
            return;
        }
    }

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/ip_mac.txt", dir_path); // Consistent file name

    FILE *fp = fopen(file_path, "w"); // Overwrite previous info for this type
    if (fp == NULL) {
        perror("[0002] Failed to open ip_mac.txt file");
        return;
    }
    fprintf(fp, "IP Address: %s\n", ip_addr_str);
    fprintf(fp, "MAC Address: %s\n", mac_addr_str);
    fclose(fp);
    printf("[0002] Saved IP/MAC info for ID %s to %s.\n", unique_id, file_path);
}

// 0003 모든 네트워크 정보 요청 처리
void handle_0003_data(const char *data, const char *client_ip_str) {

    if (strlen(data) < HEADER_LENGTH + UNIQUE_ID_LENGTH) {
        fprintf(stderr, "[0003] Invalid data length: %s\n", data);
        return;
    }

    char unique_id[UNIQUE_ID_LENGTH + 1];
    strncpy(unique_id, data + HEADER_LENGTH, UNIQUE_ID_LENGTH);
    unique_id[UNIQUE_ID_LENGTH] = '\0';

    const char *payload = data + HEADER_LENGTH + UNIQUE_ID_LENGTH; // "if_name:IP:MAC;if_name2:IP2:MAC2;"

    printf("[0003] Received all network info for ID %s from %s: %s\n", unique_id, client_ip_str, payload);

    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", BASE_DIR, unique_id);

    if (access(dir_path, F_OK) == -1) {
        if (mkdir(dir_path, 0755) == -1) {
            perror("[0003] Failed to create client directory for network info");
            return;
        }
    }

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/network_info.txt", dir_path);

    FILE *fp = fopen(file_path, "w"); // 이전 정보를 덮어씀
    if (fp == NULL) {
        perror("[0003] Failed to open network_info.txt file");
        return;
    }

    // 페이로드 파싱 및 파일에 저장
    char *temp_payload = strdup(payload);
    if (temp_payload == NULL) { perror("strdup failed"); fclose(fp); return; }

    char *token = strtok(temp_payload, ";");
    while (token != NULL) {
        fprintf(fp, "%s\n", token); // 각 인터페이스 정보를 한 줄씩 저장
        token = strtok(NULL, ";");
    }
    free(temp_payload);
    fclose(fp);
    printf("[0003] Saved all network info for ID %s to %s.\n", unique_id, file_path);
}

// Function to handle 4444 (Order file check and executable dispatch)
void handle_4444_request(int client_sock, const char *data_payload, const char *client_ip_str) {
    // data_payload should be the unique_id
    if (strlen(data_payload) != UNIQUE_ID_LENGTH) {
        fprintf(stderr, "[4444] Invalid unique ID length received: %s\n", data_payload);
        // Optionally send an error response to client
        char error_response[] = "4444ERROR_INVALID_ID";
        send(client_sock, error_response, strlen(error_response), 0);
        return;
    }

    char unique_id[UNIQUE_ID_LENGTH + 1];
    strncpy(unique_id, data_payload, UNIQUE_ID_LENGTH);
    unique_id[UNIQUE_ID_LENGTH] = '\0';

    char order_file_path[256];
    snprintf(order_file_path, sizeof(order_file_path), "%s/%s/order", BASE_DIR, unique_id);

    printf("[4444] Client %s (ID: %s) requesting order file check.\n", client_ip_str, unique_id);

    char executable_name[HEADER_LENGTH + 1] = {0};
    FILE *order_fp = fopen(order_file_path, "r");

    if (order_fp == NULL) {
        fprintf(stderr, "[4444] Order file not found for ID %s: %s. Sending '9999' as no command.\n", unique_id, order_file_path);
        // If order file doesn't exist, send a dummy 'no command' executable name
        // and then close the connection or send an empty file.
        // For this scenario, we'll send a "9999" (no command) and then nothing, so client should handle that.
        // Or, more robustly, client expects *something* to be sent.
        char no_command_exec[] = "9999";
        send(client_sock, no_command_exec, strlen(no_command_exec), 0);
        return; // No file to send
    }

    // Read the 4-digit executable name from the order file
    if (fread(executable_name, 1, HEADER_LENGTH, order_fp) != HEADER_LENGTH) {
        fprintf(stderr, "[4444] Failed to read 4-digit executable name from order file: %s. Sending '9999'.\n", order_file_path);
        fclose(order_fp);
        char no_command_exec[] = "9999";
        send(client_sock, no_command_exec, strlen(no_command_exec), 0);
        return;
    }
    executable_name[HEADER_LENGTH] = '\0';
    fclose(order_fp); // Close order file after reading

    // --- Send the requested executable file ---
    char executable_full_path[256];
    snprintf(executable_full_path, sizeof(executable_full_path), "%s/%s", HOME_DIR, executable_name);

    if (access(executable_full_path, F_OK) != 0) { // Check if executable exists on server
        fprintf(stderr, "[4444] Executable '%s' not found in HOME_DIR: %s. Sending '9999'.\n", executable_name, executable_full_path);
        char no_command_exec[] = "9999"; // Send "9999" to indicate file not found
        send(client_sock, no_command_exec, strlen(no_command_exec), 0);
        return;
    }

    if (send_file_with_header_to_client(executable_full_path, client_sock, executable_name) != 0) {
        fprintf(stderr, "[4444] Failed to send executable file '%s' to client.\n", executable_name);
        return;
    }

    // --- Post-transfer actions: Log and Clear Order File ---
    // Log the sent file
    char order_log_file_path[256];
    snprintf(order_log_file_path, sizeof(order_log_file_path), "%s/%s/order_log.txt", BASE_DIR, unique_id);
    FILE *log_fp = fopen(order_log_file_path, "a"); // Append mode
    if (log_fp) {
        time_t current_time;
        struct tm *local_time;
        char time_str[64];

        time(&current_time);
        local_time = localtime(&current_time);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time);

        fprintf(log_fp, "[%s] Sent executable: %s\n", time_str, executable_name);
        fclose(log_fp);
        printf("[4444] Logged sent executable '%s' to %s.\n", executable_name, order_log_file_path);
    } else {
        perror("[4444] Failed to open order_log.txt");
    }

    // Clear the order file
    order_fp = fopen(order_file_path, "w"); // Truncate and open for writing
    if (order_fp) {
        fclose(order_fp); // Just close to truncate
        printf("[4444] Cleared order file: %s\n", order_file_path);
    } else {
        perror("[4444] Failed to clear order file");
    }
}


// Function to handle other data types (for future expansion)
void handle_generic_data(const char *header_num, const char *data, const char *client_ip_str) {
    if (strlen(data) < HEADER_LENGTH + UNIQUE_ID_LENGTH) {
        fprintf(stderr, "[%s] Invalid data length: %s\n", header_num, data);
        return;
    }

    char unique_id[UNIQUE_ID_LENGTH + 1];
    strncpy(unique_id, data + HEADER_LENGTH, UNIQUE_ID_LENGTH);
    unique_id[UNIQUE_ID_LENGTH] = '\0';

    const char *payload = data + HEADER_LENGTH + UNIQUE_ID_LENGTH;

    printf("[%s] Received generic data for ID %s from %s: %s\n", header_num, unique_id, client_ip_str, payload);

    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", BASE_DIR, unique_id);

    if (access(dir_path, F_OK) == -1) {
        printf("[%s] Directory for ID %s does not exist. Attempting to create.\n", header_num, unique_id);
        if (mkdir(dir_path, 0755) == -1) {
            perror("[%s] Failed to create client directory for generic data");
            return;
        }
    }

    char file_path[512];
    // Example: Save to a file named after the header number
    snprintf(file_path, sizeof(file_path), "%s/%s/%s_data.txt", BASE_DIR, unique_id, header_num);

    FILE *fp = fopen(file_path, "a"); // Append mode for generic data
    if (fp == NULL) {
        perror("[%s] Failed to open data file for generic data");
        return;
    }
    fprintf(fp, "[%s] %s\n", header_num, payload);
    fclose(fp);
    printf("[%s] Saved generic data for ID %s to %s.\n", header_num, unique_id, file_path);
}


int main() {
    int tcp_listen_sock, udp_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size;
    char buffer[BUFFER_SIZE];
    
    fd_set readfds;
    int tcp_client_sockets[MAX_CLIENTS];
    int num_tcp_clients = 0;
    int max_sd, activity;

    srand(time(NULL)); // 랜덤시드

    // --- Create TCP listening socket ---
    tcp_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_listen_sock == -1) {
        perror("TCP socket creation failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    // SO_REUSEADDR 옵션 설정: 서버가 비정상 종료 후 재시작 시, 이전 바인딩이 TIME_WAIT 상태여도 즉시 재사용 가능하게 함
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
    // UDP also needs SO_REUSEADDR if you want to restart quickly
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

    // tcp 소켓 0으로 초기화
    for (int i = 0; i < MAX_CLIENTS; i++) {
        tcp_client_sockets[i] = 0;
    }

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(tcp_listen_sock, &readfds);
        FD_SET(udp_sock, &readfds); // Add UDP socket to set
        max_sd = (tcp_listen_sock > udp_sock) ? tcp_listen_sock : udp_sock;

        // Add active TCP client sockets to set
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
                if (valread == 0) { // Client disconnected
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

                        /* TCP 형식의 패킷 처리, 헤더 추가*/
                        if (strcmp(header, "0001") == 0) {
                            handle_0001_request(sd, inet_ntoa(client_addr.sin_addr)); // Pass client_ip_str from last accept or by lookup
                        } else {
                            // For TCP, other types might be expected to send a unique ID first
                            // Or, you can extend handle_generic_data to distinguish TCP/UDP source
                            printf("[TCP] Unknown or unsupported header '%s' for TCP connection. Data: %s\n", header, buffer);
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

                    /* UDP 형식의 패킷 처리, 헤더 추가
                    
                    else if (strcmp(header, "0003") == 0) {
                        handle_0003_data(buffer, inet_ntoa(client_addr.sin_addr));
                    }
                        
                    이걸 숫자만 바꿔서 추가 가능 추 후에 case 로 변경
                    */
                    if (strcmp(header, "0002") == 0) {
                        handle_0002_data(buffer, inet_ntoa(client_addr.sin_addr));
                    } 
                    else if (strcmp(header, "0003") == 0) {
                        handle_0003_data(buffer, inet_ntoa(client_addr.sin_addr));
                    }
                    else {
                        // Handle other generic UDP data types
                        handle_generic_data(header, buffer, inet_ntoa(client_addr.sin_addr));
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