#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>

#define SERVER_IP "127.0.0.1" // Change to your server's IP
#define PORT 443
#define BUFFER_SIZE 2048
#define UNIQUE_ID_LENGTH 8
#define HEADER_LENGTH 4
#define UNIQUE_ID_FILE "client_unique_id.txt" // File to store the unique ID

// Function to save unique ID to file
void save_unique_id(const char *id) {
    FILE *fp = fopen(UNIQUE_ID_FILE, "w");
    if (fp == NULL) {
        perror("Failed to open client_unique_id.txt for writing");
        return;
    }
    fprintf(fp, "%s", id);
    fclose(fp);
    printf("Unique ID saved to %s\n", UNIQUE_ID_FILE);
}

int main(int argc, char *argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char unique_id_from_server[UNIQUE_ID_LENGTH + 1] = {0};

    // Check if unique ID already exists, if so, exit (this program is for initial request)
    FILE *fp_check = fopen(UNIQUE_ID_FILE, "r");
    if (fp_check != NULL) {
        char temp_id[UNIQUE_ID_LENGTH + 1];
        if (fscanf(fp_check, "%s", temp_id) == 1 && strlen(temp_id) == UNIQUE_ID_LENGTH) {
            printf("Unique ID already exists (%s). This program is for initial request. Exiting.\n", temp_id);
            fclose(fp_check);
            exit(EXIT_SUCCESS);
        }
        fclose(fp_check);
    }


    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server.\n");

    // Send 0001 header for unique ID request
    char request_msg[HEADER_LENGTH + 1];
    strcpy(request_msg, "0001");
    printf("Sending request: %s\n", request_msg);

    if (send(sock, request_msg, strlen(request_msg), 0) < 0) {
        perror("Send 0001 request failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Receive response (should be "0001" + 8-digit ID)
    ssize_t valread = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (valread > 0) {
        buffer[valread] = '\0';
        printf("Received from server: %s\n", buffer);

        if (valread >= HEADER_LENGTH + UNIQUE_ID_LENGTH) {
            char response_header[HEADER_LENGTH + 1];
            strncpy(response_header, buffer, HEADER_LENGTH);
            response_header[HEADER_LENGTH] = '\0';

            if (strcmp(response_header, "0001") == 0) {
                strncpy(unique_id_from_server, buffer + HEADER_LENGTH, UNIQUE_ID_LENGTH);
                unique_id_from_server[UNIQUE_ID_LENGTH] = '\0';
                printf("Received unique ID: %s\n", unique_id_from_server);
                save_unique_id(unique_id_from_server);
            } else {
                printf("Unexpected response header: %s\n", response_header);
            }
        } else {
            printf("Malformed response from server.\n");
        }
    } else if (valread == 0) {
        printf("Server disconnected.\n");
    } else {
        perror("Receive failed");
    }

    close(sock);
    return 0;
}