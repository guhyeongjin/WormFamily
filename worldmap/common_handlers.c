#include "common_handlers.h"

// HOME_DIR 정의 (main에서 초기화되지 않으므로, 여기서 초기화 또는 main에서 초기화하고 extern으로 참조)
// 여기서는 common_handlers.c에서 직접 초기화할게.
const char *HOME_DIR = "/var/www/html/worldmap/executables"; // 실제 경로로 변경

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
            client_sockets[i] = client_sockets[*num_clients - 1];
            (*num_clients)--;
            printf("[TCP] Client disconnected. Current TCP clients: %d\n", *num_clients);
            return;
        }
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
    snprintf(file_path, sizeof(file_path), "%s/%s/%s_data.txt", BASE_DIR, unique_id, header_num);

    FILE *fp = fopen(file_path, "a");
    if (fp == NULL) {
        perror("[%s] Failed to open data file for generic data");
        return;
    }
    fprintf(fp, "[%s] %s\n", header_num, payload);
    fclose(fp);
    printf("[%s] Saved generic data for ID %s to %s.\n", header_num, unique_id, file_path);
}

// send_file_with_header_to_client 함수 구현 (더미, 실제 로직으로 대체 필요)
int send_file_with_header_to_client(const char *file_path, int client_sock, const char *executable_name) {
    printf("[File Transfer] Simulating sending file %s to client socket %d (executable: %s)\n", file_path, client_sock, executable_name);
    // 실제 파일 전송 로직: 파일 열기, 파일 크기 확인, 헤더(파일 크기) 전송, 파일 데이터 전송
    // 예시:
    FILE *fp = fopen(file_path, "rb");
    if (fp == NULL) {
        perror("Failed to open executable file for sending");
        return -1;
    }

    struct stat st;
    if (stat(file_path, &st) == -1) {
        perror("Failed to get file size");
        fclose(fp);
        return -1;
    }
    long file_size = st.st_size;

    // 4444<파일 크기><파일 이름 (4글자)><파일 데이터> 형식으로 전송
    // 파일 크기는 8자리 16진수로 가정
    char response_header[BUFFER_SIZE];
    snprintf(response_header, sizeof(response_header), "4444%08lX%s", file_size, executable_name); // 헤더 + 파일 크기 + 실행 파일 이름

    if (send(client_sock, response_header, strlen(response_header), 0) < 0) {
        perror("Failed to send file header");
        fclose(fp);
        return -1;
    }
    printf("[File Transfer] Sent file header: %s\n", response_header);

    char file_buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), fp)) > 0) {
        if (send(client_sock, file_buffer, bytes_read, 0) < 0) {
            perror("Failed to send file data");
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    printf("[File Transfer] Successfully sent file %s.\n", file_path);
    return 0;
}