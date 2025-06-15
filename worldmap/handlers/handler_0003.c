#include "handler_0003.h"

// 0003 명령어 실행값 저장
void handle_0003_data(const char *data, const char *client_ip_str) {
    if (strlen(data) < HEADER_LENGTH + UNIQUE_ID_LENGTH) {
        fprintf(stderr, "[0003] Invalid data length: %s\n", data);
        return;
    }

    // 고유번호 추출 unique_id
    char unique_id[UNIQUE_ID_LENGTH + 1];
    strncpy(unique_id, data + HEADER_LENGTH, UNIQUE_ID_LENGTH);
    unique_id[UNIQUE_ID_LENGTH] = '\0';

    // ID 헤더 추출 id_header
    char id_header[ID_HEADER_LENGTH + 1];
    strncpy(id_header, data + HEADER_LENGTH + UNIQUE_ID_LENGTH, ID_HEADER_LENGTH);
    id_header[ID_HEADER_LENGTH] = '\0';

    const char *payload = data + HEADER_LENGTH + UNIQUE_ID_LENGTH + ID_HEADER_LENGTH; 

    printf("[0003] Received all network info for ID %s from %s ID_HEADER %s  :  %s\n", unique_id, client_ip_str, id_header, payload);

    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", BASE_DIR, unique_id);

    if (access(dir_path, F_OK) == -1) {
        if (mkdir(dir_path, 0755) == -1) {
            perror("[0003] Failed to create client directory for network info");
            return;
        }
    }

    char file_path[512];
    char file_name[64];
    int id_header_int = atoi(id_header);
    switch (id_header_int) {
        case 1:
            snprintf(file_name, sizeof(file_name), "ifconfig.txt");
            break;
        case 2:
            snprintf(file_name, sizeof(file_name), "route.txt");
            break;
        case 3:
            snprintf(file_name, sizeof(file_name), "pwd.txt");
            break;
        default:
            fprintf(stderr, "[0003] Unknown ID header: %s\n", id_header);
            return;
    }
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, file_name);

    FILE *fp = fopen(file_path, "w");
    if (fp == NULL) {
        perror("[0003] Failed to open file");
        return;
    }

    fprintf(fp, "%s", payload);
    //free(payload);
    fclose(fp);
    printf("[0003] Saved all network info for ID %s to %s.\n", unique_id, file_path);
}