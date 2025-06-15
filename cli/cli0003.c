#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>

#define SERVER_IP "127.0.0.1" // 서버의 IP 주소로 변경하세요!
#define PORT 443
#define BUFFER_SIZE 4096 // ifconfig 출력 전체를 담기 위해 충분히 크게 설정
#define UNIQUE_ID_LENGTH 8
#define HEADER_LENGTH 4
#define UNIQUE_ID_FILE "client_unique_id.txt" // 클라이언트의 고유 ID가 저장될 파일

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

// --- ifconfig 명령어의 모든 결과를 가져오는 함수 ---
// info_buffer에 ifconfig의 전체 출력을 그대로 저장
void get_ifconfig_output(char *info_buffer, size_t buffer_size) {
    FILE *fp;
    char line[256];
    info_buffer[0] = '\0'; // 버퍼 초기화
    size_t current_len = 0;

    // ifconfig 명령 실행
    fp = popen("ifconfig", "r");
    if (fp == NULL) {
        perror("popen for ifconfig");
        strncpy(info_buffer, "ERROR_GETTING_IFCONFIG_INFO", buffer_size - 1);
        info_buffer[buffer_size - 1] = '\0';
        return;
    }

    // 각 라인을 읽어 info_buffer에 추가
    while (fgets(line, sizeof(line), fp) != NULL) {
        current_len = strlen(info_buffer);
        size_t line_len = strlen(line);

        // 버퍼 오버플로우 방지 및 라인 끝 개행 문자 유지
        if (current_len + line_len + 1 > buffer_size) { // +1 for null terminator
            fprintf(stderr, "Buffer overflow prevented during ifconfig output capture.\n");
            break; // 버퍼가 가득 찼으면 중단
        }
        strncat(info_buffer, line, buffer_size - current_len - 1);
    }
    pclose(fp);
    info_buffer[buffer_size - 1] = '\0'; // 최종적으로 NULL 종료
}

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char unique_id[UNIQUE_ID_LENGTH + 1] = {0};
    // ifconfig 출력을 담을 버퍼
    char ifconfig_output[BUFFER_SIZE - HEADER_LENGTH - UNIQUE_ID_LENGTH - 1] = {0};

    // 고유 ID 로드 시도
    if (!load_unique_id(unique_id)) {
        printf("Unique ID not found or malformed in %s. Please run client_0001 first to get an ID.\n", UNIQUE_ID_FILE);
        exit(EXIT_FAILURE);
    }
    printf("Loaded unique ID: %s\n", unique_id);

    // UDP 소켓 생성
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP Socket creation error");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 서버 IP 주소 설정
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // --- ifconfig 결과 가져오는 함수 호출 ---
    get_ifconfig_output(ifconfig_output, sizeof(ifconfig_output));
    printf("Collected ifconfig Output:\n%s\n", ifconfig_output);

    // 새로운 형식: "0003<8-digit-ID><ifconfig_output_dump>"
    snprintf(buffer, BUFFER_SIZE, "%s%s%s%s", "0003", unique_id, "0001", ifconfig_output);
    printf("Sending data: %s (Length: %zu)\n", buffer, strlen(buffer));

    // 서버로 데이터 전송
    if (sendto(sock, buffer, strlen(buffer), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Sendto failed");
    } else {
        printf("ifconfig output sent successfully.\n");
    }

    close(sock);
    return 0;
}