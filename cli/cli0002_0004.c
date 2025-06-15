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
#include <netinet/ip_icmp.h> // ICMP header (often icmp.h or ip_icmp.h)

#define SERVER_IP "127.0.0.1"    // 서버로 결과 전송할 IP
#define TRACERT_IP "218.154.144.144"     // Traceroute 대상 IP
#define SERVER_RESULT_PORT 443

#define BUFFER_SIZE 4096         // 버퍼 크기
#define UNIQUE_ID_LENGTH 8
#define HEADER_LENGTH 4 
#define ID_HEADER_LENGTH 4
#define UNIQUE_ID_FILE "client_unique_id.txt" // 클라이언트의 고유 ID가 저장될 파일

#define MAX_HOPS 30              // 최대 홉 수 (트레이스루트 기본값)
#define RECV_TIMEOUT_SEC 2       // 각 홉에서의 응답 대기 시간 (초)

// ICMP 체크섬 계산 함수
unsigned short in_cksum(unsigned short *addr, int len) {
    int nleft = len;
    int sum = 0;
    unsigned short *w = addr;
    unsigned short answer = 0;

    while (nleft > 1) {
        sum += *w++;
        nleft -= 2;
    }

    if (nleft == 1) {
        *(unsigned char *)&answer = *(unsigned char *)w;
        sum += answer;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    return (answer);
}

// 고유번호 로드
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

// ICMP 트레이스루트 실행 및 경로 정보 수집 함수
void perform_icmp_traceroute_and_collect_hops(const char *tracert_ip, char *hops_buffer, size_t buffer_size) {
    int sock_fd;
    struct sockaddr_in dest_addr, recv_addr;
    socklen_t addr_len = sizeof(recv_addr);
    char send_buf[128]; // ICMP 헤더 + 데이터
    char recv_buf[BUFFER_SIZE]; // IP 헤더 + ICMP 헤더 + 원본 IP/ICMP 헤더 + 데이터

    struct icmphdr *icmp_hdr_send;
    struct iphdr *ip_hdr_recv;
    struct icmphdr *icmp_hdr_recv;

    struct timeval tv;
    hops_buffer[0] = '\0'; // 결과 버퍼 초기화
    size_t current_hops_len = 0;
    int pid = getpid(); // 식별자로 프로세스 ID 사용

    printf("Starting ICMP traceroute to %s...\n", tracert_ip);

    // RAW ICMP 소켓 생성 (루트 권한 필요!)
    if ((sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
        perror("RAW ICMP Socket creation error. Root privileges required?");
        strncpy(hops_buffer, "ERROR_SOCKET_CREATION", buffer_size);
        return;
    }

    // 수신 타임아웃 설정
    tv.tv_sec = RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
        close(sock_fd);
        strncpy(hops_buffer, "ERROR_SET_TIMEOUT", buffer_size);
        return;
    }

    // 목적지 주소 설정
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, tracert_ip, &dest_addr.sin_addr) <= 0) {
        printf("\nInvalid traceroute IP address \n");
        close(sock_fd);
        strncpy(hops_buffer, "ERROR_INVALID_TRACERT_IP", buffer_size);
        return;
    }

    for (int ttl = 1; ttl <= MAX_HOPS; ttl++) {
        char hop_ip_str[INET_ADDRSTRLEN];
        memset(send_buf, 0, sizeof(send_buf));
        icmp_hdr_send = (struct icmphdr *)send_buf;

        // ICMP 에코 요청 패킷 구성
        icmp_hdr_send->type = ICMP_ECHO; // Type 8
        icmp_hdr_send->code = 0;
        // --- 여기부터 수정 ---
        icmp_hdr_send->un.echo.id = pid;       // 식별자로 PID 사용
        icmp_hdr_send->un.echo.sequence = ttl;   // 시퀀스 번호로 TTL 사용
        // --- 수정 끝 ---
        icmp_hdr_send->checksum = 0;   // 체크섬은 나중에 계산

        // 임의 데이터 추가 (옵션)
        strncpy((char *)(send_buf + sizeof(struct icmphdr)), "HELLO", sizeof(send_buf) - sizeof(struct icmphdr) - 1);

        // ICMP 체크섬 계산
        icmp_hdr_send->checksum = in_cksum((unsigned short *)send_buf, sizeof(struct icmphdr) + strlen((char *)(send_buf + sizeof(struct icmphdr))));


        // TTL 설정
        if (setsockopt(sock_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
            perror("setsockopt IP_TTL failed");
            // 치명적이지 않으므로 계속 진행하거나 루프 종료 고려
            break;
        }

        printf("TTL %2d: Sending ICMP packet to %s...\n", ttl, tracert_ip);

        // ICMP 패킷 전송
        if (sendto(sock_fd, send_buf, sizeof(struct icmphdr) + strlen((char *)(send_buf + sizeof(struct icmphdr))), 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
            perror("sendto failed");
            break;
        }

        // 응답 수신
        ssize_t bytes_received = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&recv_addr, &addr_len);

        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("  Timeout.\n");
                strncpy(hop_ip_str, "*", sizeof(hop_ip_str)); // 타임아웃 표시
            } else {
                perror("recvfrom error");
                strncpy(hop_ip_str, "ERR", sizeof(hop_ip_str)); // 기타 오류
            }
        } else {
            // IP 헤더와 ICMP 헤더 파싱
            ip_hdr_recv = (struct iphdr *)recv_buf;
            // ihl은 4바이트 단위이므로 * 4를 통해 실제 바이트 길이 계산
            // IP 헤더 뒤에 ICMP 헤더가 오므로, IP 헤더 길이를 건너뛰어야 함
            if ((ip_hdr_recv->ihl << 2) + sizeof(struct icmphdr) > bytes_received) {
                fprintf(stderr, "Received packet too short for ICMP header. Bytes received: %zd, expected: %zu\n",
                        bytes_received, (ip_hdr_recv->ihl << 2) + sizeof(struct icmphdr));
                strncpy(hop_ip_str, "MALFORMED_PACKET", sizeof(hop_ip_str));
            } else {
                icmp_hdr_recv = (struct icmphdr *)(recv_buf + (ip_hdr_recv->ihl << 2));

                inet_ntop(AF_INET, &(recv_addr.sin_addr), hop_ip_str, sizeof(hop_ip_str));

                // ICMP 타입 확인
                if (icmp_hdr_recv->type == ICMP_TIMXCEED && icmp_hdr_recv->code == ICMP_TIMXCEED_INTRANS) { // Type 11, Code 0
                    printf("  Time Exceeded from %s\n", hop_ip_str);
                } else if (icmp_hdr_recv->type == ICMP_ECHOREPLY) { // Type 0 (Echo Reply)
                    // --- 여기부터 수정 ---
                    // ICMP Echo Reply 패킷에서 id와 sequence를 un.echo를 통해 접근
                    if (icmp_hdr_recv->un.echo.id == pid && icmp_hdr_recv->un.echo.sequence == ttl) {
                        printf("  Echo Reply from %s. Reached destination.\n", hop_ip_str);
                        current_hops_len += snprintf(hops_buffer + current_hops_len, buffer_size - current_hops_len,
                                                    "%s;", hop_ip_str);
                        goto end_traceroute; // 목적지 도달 시 루프 종료
                    } else {
                        // PID나 시퀀스 번호가 일치하지 않는 에코 응답 (다른 프로그램/트래픽)
                        printf("  Echo Reply from %s (ID/Seq mismatch: %d/%d vs %d/%d)\n", hop_ip_str,
                               icmp_hdr_recv->un.echo.id, icmp_hdr_recv->un.echo.sequence, pid, ttl);
                        strncpy(hop_ip_str, "UNMATCHED_REPLY", sizeof(hop_ip_str));
                    }
                    // --- 수정 끝 ---
                } else if (icmp_hdr_recv->type == ICMP_DEST_UNREACH) { // Type 3 (Destination Unreachable)
                    printf("  Destination Unreachable from %s (Type 3, Code %d)\n", hop_ip_str, icmp_hdr_recv->code);
                    current_hops_len += snprintf(hops_buffer + current_hops_len, buffer_size - current_hops_len,
                                                "%s;", hop_ip_str);
                    goto end_traceroute; // 목적지 도달 불가 시 루프 종료
                } else {
                    printf("  Received unknown ICMP type %d from %s\n", icmp_hdr_recv->type, hop_ip_str);
                }
            }
        }

        current_hops_len += snprintf(hops_buffer + current_hops_len, buffer_size - current_hops_len,
                                    "%s;", hop_ip_str);
        if (current_hops_len >= buffer_size - (INET_ADDRSTRLEN + 2)) {
            fprintf(stderr, "Hops buffer overflow. Truncating results.\n");
            break;
        }
    }
    close(sock_fd);

end_traceroute:
    printf("Traceroute finished.\n");
    // 마지막 세미콜론이 있다면 제거
    if (current_hops_len > 0 && hops_buffer[current_hops_len - 1] == ';') {
        hops_buffer[current_hops_len - 1] = '\0';
    }
}


int main(int argc, char *argv[]) {
    char unique_id[UNIQUE_ID_LENGTH + 1] = {0};
    char hops_collected[BUFFER_SIZE - HEADER_LENGTH - UNIQUE_ID_LENGTH - ID_HEADER_LENGTH - 1] = {0}; // 홉 주소들을 담을 버퍼

    // 고유 ID 로드
    if (!load_unique_id(unique_id)) {
        printf("Unique ID not found or malformed in %s. Please run client_0001 first to get an ID.\n", UNIQUE_ID_FILE);
        exit(EXIT_FAILURE);
    }
    printf("Loaded unique ID: %s\n", unique_id);

    // ICMP Traceroute 실행 및 홉 주소 수집
    // TRACERT_IP는 추적할 목적지 IP를 의미
    perform_icmp_traceroute_and_collect_hops(TRACERT_IP, hops_collected, sizeof(hops_collected));
    printf("Collected Hop Addresses: %s\n", hops_collected);

    // 이제 이 홉 주소 데이터를 TCP로 서버에 전송
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

    // 최종 데이터 형식: "0002<8-digit-ID><0004><TTL1_Hop_IP;TTL2_Hop_IP;...>"
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