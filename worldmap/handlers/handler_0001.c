#include "handler_0001.h"

// 0001 unique id 요청 처리
void handle_0001_request(int client_sock, const char *client_ip_str) {
    char unique_id[UNIQUE_ID_LENGTH + 1] = {0};
    generate_unique_id(unique_id); // common_handlers.c의 함수 사용

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
    snprintf(response_buffer, sizeof(response_buffer), "0001%s", unique_id);

    if (send(client_sock, response_buffer, strlen(response_buffer), 0) < 0) {
        perror("Failed to send unique ID via TCP");
    } else {
        printf("[0001] Sent unique ID '%s' to client %s.\n", unique_id, client_ip_str);
    }
}