#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h> // for struct timeval
#include <fcntl.h>    // for non-blocking socket
#include <errno.h>
#include <netinet/ip.h>   // IP header


#define SERVER_IP "127.0.0.1"    // 서버로 결과 전송할 IP
#define SERVER_RESULT_PORT 443   // 수집된 정보 전송에 사용할 서버 포트

#define BUFFER_SIZE 4096         // 버퍼 크기
#define UNIQUE_ID_LENGTH 8
#define HEADER_LENGTH 4
#define ID_HEADER_LENGTH 4
#define UNIQUE_ID_FILE "client_unique_id.txt" // 클라이언트의 고유 ID가 저장될 파일

// 고유번호 로드 해당 경로의 client_unique_id.txt에 저장되어 있음
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


int main(int argc, char *argv[]) {
    char unique_id[UNIQUE_ID_LENGTH + 1] = {0};
    char hops_collected[BUFFER_SIZE - HEADER_LENGTH - UNIQUE_ID_LENGTH - ID_HEADER_LENGTH - 1] = {0}; // 홉 주소들을 담을 버퍼

    // 고유 ID 로드 unique_id
    if (!load_unique_id(unique_id)) {
        printf("Unique ID not found or malformed in %s. Please run client_0001 first to get an ID.\n", UNIQUE_ID_FILE);
        exit(EXIT_FAILURE);
    }
    printf("Loaded unique ID: %s\n", unique_id);

    // 주 기능 함수 사용 예시
    perform_icmp_traceroute_and_collect_hops(TRACERT_IP, hops_collected, sizeof(hops_collected));
    printf("Collected Hop Addresses: %s\n", hops_collected);

   
    int final_tcp_sock;
    struct sockaddr_in serv_addr_tcp;
    char final_buffer_tcp[BUFFER_SIZE] = {0};

    // TCP 소켓 생성 (최종 전송용)
    if ((final_tcp_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Final TCP Socket creation error");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr_tcp, 0, sizeof(serv_addr_tcp));
    serv_addr_tcp.sin_family = AF_INET;
    serv_addr_tcp.sin_port = htons(SERVER_RESULT_PORT); // 서버의 TCP 포트 (443)

    // 서버 IP 주소 설정
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr_tcp.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        close(final_tcp_sock);
        exit(EXIT_FAILURE);
    }

    printf("Connecting to server %s:%d for result transmission...\n", SERVER_IP, SERVER_RESULT_PORT);
    // 서버와 TCP 연결
    if (connect(final_tcp_sock, (struct sockaddr *)&serv_addr_tcp, sizeof(serv_addr_tcp)) < 0) {
        perror("Final TCP connect failed");
        close(final_tcp_sock);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server.\n");

    // 최종 데이터 형식: "<헤더><8-digit-ID><ID헤더><payload>"
    snprintf(final_buffer_tcp, BUFFER_SIZE, "%s%s%s%s", "0002", unique_id, "0004", hops_collected);
    printf("Sending final traceroute data to server (TCP): %s (Length: %zu)\n", final_buffer_tcp, strlen(final_buffer_tcp));

    // 서버로 TCP 데이터 전송
    if (send(final_tcp_sock, final_buffer_tcp, strlen(final_buffer_tcp), 0) < 0) {
        perror("Final TCP send failed");
    } else {
        printf("Traceroute data sent successfully via TCP to server.\n");
    }

    close(final_tcp_sock);
    return 0;
}