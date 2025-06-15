#include "handler_4444.h"

// Function to handle 4444 (Order file check and executable dispatch)
void handle_4444_request(int client_sock, const char *data_payload, const char *client_ip_str) {
    if (strlen(data_payload) != UNIQUE_ID_LENGTH) {
        fprintf(stderr, "[4444] Invalid unique ID length received: %s\n", data_payload);
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
        char no_command_exec[] = "9999";
        send(client_sock, no_command_exec, strlen(no_command_exec), 0);
        return;
    }

    if (fread(executable_name, 1, HEADER_LENGTH, order_fp) != HEADER_LENGTH) {
        fprintf(stderr, "[4444] Failed to read 4-digit executable name from order file: %s. Sending '9999'.\n", order_file_path);
        fclose(order_fp);
        char no_command_exec[] = "9999";
        send(client_sock, no_command_exec, strlen(no_command_exec), 0);
        return;
    }
    executable_name[HEADER_LENGTH] = '\0';
    fclose(order_fp);

    char executable_full_path[256];
    snprintf(executable_full_path, sizeof(executable_full_path), "%s/%s", HOME_DIR, executable_name);

    if (access(executable_full_path, F_OK) != 0) {
        fprintf(stderr, "[4444] Executable '%s' not found in HOME_DIR: %s. Sending '9999'.\n", executable_name, executable_full_path);
        char no_command_exec[] = "9999";
        send(client_sock, no_command_exec, strlen(no_command_exec), 0);
        return;
    }

    if (send_file_with_header_to_client(executable_full_path, client_sock, executable_name) != 0) {
        fprintf(stderr, "[4444] Failed to send executable file '%s' to client.\n", executable_name);
        return;
    }

    char order_log_file_path[256];
    snprintf(order_log_file_path, sizeof(order_log_file_path), "%s/%s/order_log.txt", BASE_DIR, unique_id);
    FILE *log_fp = fopen(order_log_file_path, "a");
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

    order_fp = fopen(order_file_path, "w");
    if (order_fp) {
        fclose(order_fp);
        printf("[4444] Cleared order file: %s\n", order_file_path);
    } else {
        perror("[4444] Failed to clear order file");
    }
}