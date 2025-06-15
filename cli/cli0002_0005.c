#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>     // For ifreq, IFNAMSIZ, IFF_UP, IFF_LOOPBACK
#include <netinet/in.h> // For sockaddr_in
#include <linux/if_packet.h> // For sockaddr_ll
#include <linux/if_ether.h> // For ETH_P_ARP, ETH_ALEN, ETH_HLEN
#include <linux/if_arp.h>   // For ARPHRD_ETHER, ARPOP_REQUEST, ARPOP_REPLY
#include <sys/time.h>   // For struct timeval
#include <errno.h>

#define SERVER_IP "127.0.0.1" // 서버로 ARP 테이블 전송할 IP
#define SERVER_RESULT_PORT 443 // 수집된 정보 전송에 사용할 서버 포트

#define BUFFER_SIZE 4096         // 버퍼 크기
#define UNIQUE_ID_LENGTH 8
#define HEADER_LENGTH 4          // 메인 헤더 길이 (0002)
#define ID_HEADER_LENGTH 4       // 서브 헤더 길이 (0005)
#define UNIQUE_ID_FILE "client_unique_id.txt" // 클라이언트의 고유 ID가 저장될 파일

#define ARP_REQUEST_TIMEOUT_SEC 1 // 각 ARP 요청 응답 대기 시간

// 이더넷 헤더
struct eth_header {
    unsigned char  ether_dhost[ETH_ALEN];    // 대상 MAC 주소
    unsigned char  ether_shost[ETH_ALEN];    // 송신 MAC 주소
    unsigned short ether_type;               // 프로토콜 타입 (ETH_P_ARP)
};

// ARP 헤더
struct arp_header {
    unsigned short ar_hrd;        // 하드웨어 타입 (이더넷의 경우 ARPHRD_ETHER)
    unsigned short ar_pro;        // 프로토콜 타입 (IPv4의 경우 ETH_P_IP)
    unsigned char  ar_hln;        // 하드웨어 주소 길이 (MAC 주소의 경우 ETH_ALEN)
    unsigned char  ar_pln;        // 프로토콜 주소 길이 (IPv4 주소의 경우 4)
    unsigned short ar_op;         // ARP 동작 (ARPOP_REQUEST, ARPOP_REPLY)
    unsigned char  ar_sha[ETH_ALEN]; // 송신 MAC 주소
    unsigned int   ar_sip;        // 송신 IP 주소
    unsigned char  ar_tha[ETH_ALEN]; // 대상 MAC 주소
    unsigned int   ar_tip;        // 대상 IP 주소
};


// 고유번호 로드
int load_unique_id(char *id_buffer) {
    FILE *fp = fopen(UNIQUE_ID_FILE, "r");
    if (fp == NULL) {
        return 0;
    }
    if (fscanf(fp, "%s", id_buffer) == 1 && strlen(id_buffer) == UNIQUE_ID_LENGTH) {
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}

// 인터페이스 정보 (IP, MAC, 인덱스, 서브넷 마스크) 획득
int get_interface_info(const char *if_name, unsigned int *ip_addr, unsigned int *netmask, unsigned char *mac_addr, int *if_index) {
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    strncpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(fd);
        return -1;
    }
    *if_index = ifr.ifr_ifindex;

    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        perror("SIOCGIFADDR");
        close(fd);
        return -1;
    }
    *ip_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;

    if (ioctl(fd, SIOCGIFNETMASK, &ifr) < 0) {
        perror("SIOCGIFNETMASK");
        close(fd);
        return -1;
    }
    *netmask = ((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr.s_addr;

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("SIOCGIFHWADDR");
        close(fd);
        return -1;
    }
    memcpy(mac_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    close(fd);
    return 0;
}

// 활성 상태의 첫 번째 비-루프백 인터페이스 이름 찾기
int find_active_interface_name(char *if_name_buffer, size_t buffer_size) {
    struct ifconf ifc;
    struct ifreq *ifr;
    char buf[1024];
    int sock_fd;
    int found = 0;

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock_fd, SIOCGIFCONF, &ifc) < 0) {
        perror("SIOCGIFCONF");
        close(sock_fd);
        return -1;
    }

    ifr = ifc.ifc_req;
    for (int i = 0; i < ifc.ifc_len / sizeof(struct ifreq); i++) {
        struct ifreq current_ifr = ifr[i];

        // 인터페이스 플래그 가져오기 (UP, LOOPBACK 등)
        if (ioctl(sock_fd, SIOCGIFFLAGS, &current_ifr) < 0) {
            perror("SIOCGIFFLAGS");
            continue;
        }

        // UP 상태이고, 루프백이 아닌 인터페이스 찾기
        if ((current_ifr.ifr_flags & IFF_UP) && !(current_ifr.ifr_flags & IFF_LOOPBACK)) {
            // IP 주소도 있는지 확인 (브리지 인터페이스 등 제외)
            if (ioctl(sock_fd, SIOCGIFADDR, &current_ifr) < 0) {
                // IP 주소가 없는 인터페이스일 수 있음 (예: 브리지의 구성 요소)
                continue;
            }

            strncpy(if_name_buffer, current_ifr.ifr_name, buffer_size - 1);
            if_name_buffer[buffer_size - 1] = '\0';
            found = 1;
            break;
        }
    }

    close(sock_fd);
    return found;
}


// ARP 요청 전송 및 응답 수신
void perform_arp_scan_and_collect_table(const char *if_name, char *arp_table_buffer, size_t buffer_size) {
    int sock_fd;
    unsigned char buffer[BUFFER_SIZE]; // ARP 패킷 송수신 버퍼
    struct eth_header *eth_hdr = (struct eth_header *)buffer;
    struct arp_header *arp_hdr = (struct arp_header *)(buffer + ETH_HLEN);

    unsigned int local_ip = 0;
    unsigned int local_netmask = 0;
    unsigned char local_mac[ETH_ALEN];
    int if_index = 0;

    arp_table_buffer[0] = '\0'; // 결과 버퍼 초기화
    size_t current_len = 0;

    printf("Starting ARP scan on interface %s...\n", if_name);

    // 인터페이스 정보 획득
    if (get_interface_info(if_name, &local_ip, &local_netmask, local_mac, &if_index) < 0) {
        strncpy(arp_table_buffer, "ERROR_GET_IF_INFO", buffer_size);
        fprintf(stderr, "Failed to get info for interface %s. Make sure it's active and has an IP.\n", if_name);
        return;
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_ip, ip_str, sizeof(ip_str));
    printf("Local IP: %s\n", ip_str);

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);
    printf("Local MAC: %s\n", mac_str);

    // RAW 소켓 생성 (ETH_P_ARP 프로토콜 필터링)
    sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (sock_fd < 0) {
        perror("RAW socket creation error. Root privileges required?");
        strncpy(arp_table_buffer, "ERROR_RAW_SOCKET", buffer_size);
        return;
    }

    // 소켓 바인딩 (특정 인터페이스를 통해 패킷을 보내기 위해)
    struct sockaddr_ll sa_ll;
    memset(&sa_ll, 0, sizeof(sa_ll));
    sa_ll.sll_family = AF_PACKET;
    sa_ll.sll_protocol = htons(ETH_P_ARP);
    sa_ll.sll_ifindex = if_index; // 인터페이스 인덱스 사용

    if (bind(sock_fd, (struct sockaddr*)&sa_ll, sizeof(sa_ll)) < 0) {
        perror("socket bind failed");
        close(sock_fd);
        strncpy(arp_table_buffer, "ERROR_BIND_SOCKET", buffer_size);
        return;
    }

    // 수신 타임아웃 설정
    struct timeval tv;
    tv.tv_sec = ARP_REQUEST_TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
        close(sock_fd);
        strncpy(arp_table_buffer, "ERROR_SET_TIMEOUT", buffer_size);
        return;
    }


    unsigned int network_addr = local_ip & local_netmask;
    unsigned int broadcast_addr_ip = network_addr | (~local_netmask); // IP 브로드캐스트 주소

    printf("Scanning network %s/%s...\n", inet_ntoa(*(struct in_addr*)&network_addr),
           inet_ntoa(*(struct in_addr*)&local_netmask));


    // ARP 요청 패킷 구성 (브로드캐스트용)
    memset(eth_hdr->ether_dhost, 0xFF, ETH_ALEN); // 대상 MAC: 브로드캐스트 FF:FF:FF:FF:FF:FF
    memcpy(eth_hdr->ether_shost, local_mac, ETH_ALEN); // 송신 MAC: 로컬 MAC
    eth_hdr->ether_type = htons(ETH_P_ARP); // 프로토콜 타입: ARP

    arp_hdr->ar_hrd = htons(ARPHRD_ETHER);    // 하드웨어 타입 (이더넷)
    arp_hdr->ar_pro = htons(ETH_P_IP);        // 프로토콜 타입 (IPv4)
    arp_hdr->ar_hln = ETH_ALEN;               // 하드웨어 주소 길이
    arp_hdr->ar_pln = 4;                      // 프로토콜 주소 길이 (IP 주소)
    arp_hdr->ar_op = htons(ARPOP_REQUEST);    // ARP 동작: 요청
    memcpy(arp_hdr->ar_sha, local_mac, ETH_ALEN); // 송신 MAC
    arp_hdr->ar_sip = local_ip;               // 송신 IP
    memset(arp_hdr->ar_tha, 0x00, ETH_ALEN); // 대상 MAC (알 수 없음, 00:00:00:00:00:00)
    // arp_hdr->ar_tip 는 각 대상 IP에 따라 루프 안에서 설정


    // 네트워크 내 모든 IP에 대해 ARP 요청 전송
    // .0과 브로드캐스트 주소를 포함하여 1부터 254까지 순회
    // network_addr은 네트워크 주소이므로 i=0, broadcast_addr_ip는 브로드캐스트 주소이므로 ~local_netmask 값
    // 따라서, 1부터 (~local_netmask - 1)까지 (호스트 주소 범위)
    for (unsigned int i = 1; i < (unsigned int)ntohl(~local_netmask); i++) {
        unsigned int target_ip = network_addr + htonl(i);

        arp_hdr->ar_tip = target_ip; // 대상 IP 설정

        if (sendto(sock_fd, buffer, ETH_HLEN + sizeof(struct arp_header), 0,
                   (struct sockaddr*)&sa_ll, sizeof(sa_ll)) < 0) {
            perror("sendto ARP request failed");
        }
        usleep(1000); // 1ms 딜레이
    }

    printf("Waiting for ARP replies...\n");

    // ARP 응답 수신 및 ARP 테이블 구성
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        // sockaddr_ll 구조체는 이더넷 헤더를 수신할 때 송신자의 링크 계층 주소를 포함
        // 그러나 여기서는 주소를 사용하지 않으므로 NULL, NULL을 전달
        ssize_t bytes_received = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, NULL); 

        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("ARP reply receive timeout. Scan complete.\n");
                break; // 타임아웃, 더 이상 응답이 없음
            } else {
                perror("recvfrom ARP reply error");
                break;
            }
        }

        if (bytes_received < ETH_HLEN + sizeof(struct arp_header)) {
            fprintf(stderr, "Received truncated packet (size: %zd).\n", bytes_received);
            continue;
        }

        // 수신된 패킷이 ARP 응답인지 확인
        // 이더넷 타입이 ARP이고, ARP 동작이 응답(ARPOP_REPLY)이며, 대상 MAC이 자신의 MAC 주소와 일치하는지 확인
        // (네트워크에 다른 ARP 트래픽도 있을 수 있으므로 필터링)
        if (ntohs(eth_hdr->ether_type) == ETH_P_ARP && ntohs(arp_hdr->ar_op) == ARPOP_REPLY &&
            memcmp(eth_hdr->ether_dhost, local_mac, ETH_ALEN) == 0) { // 자신의 MAC으로 온 응답인지 확인
            char sender_ip_str[INET_ADDRSTRLEN];
            char sender_mac_str[18];

            inet_ntop(AF_INET, &arp_hdr->ar_sip, sender_ip_str, sizeof(sender_ip_str));
            snprintf(sender_mac_str, sizeof(sender_mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                     arp_hdr->ar_sha[0], arp_hdr->ar_sha[1], arp_hdr->ar_sha[2],
                     arp_hdr->ar_sha[3], arp_hdr->ar_sha[4], arp_hdr->ar_sha[5]);

            // 로컬 IP는 ARP 테이블에 포함하지 않음 (자기 자신)
            if (local_ip != arp_hdr->ar_sip) {
                printf("  Found: IP %s -> MAC %s\n", sender_ip_str, sender_mac_str);

                current_len = strlen(arp_table_buffer);
                // IP:MAC; 형식으로 추가
                if (current_len + strlen(sender_ip_str) + strlen(sender_mac_str) + 3 < buffer_size) { // +3 for ':', ';', '\0'
                    snprintf(arp_table_buffer + current_len, buffer_size - current_len,
                             "%s:%s;", sender_ip_str, sender_mac_str);
                } else {
                    fprintf(stderr, "ARP table buffer overflow. Truncating results.\n");
                    break;
                }
            }
        }
    }
    close(sock_fd);

    // 마지막 세미콜론 제거
    if (current_len > 0 && arp_table_buffer[current_len - 1] == ';') {
        arp_table_buffer[current_len - 1] = '\0';
    }
    printf("ARP scan finished. Collected entries: %s\n", arp_table_buffer);
}


int main(int argc, char *argv[]) {
    char unique_id[UNIQUE_ID_LENGTH + 1] = {0};
    char arp_table_collected[BUFFER_SIZE - HEADER_LENGTH - UNIQUE_ID_LENGTH - ID_HEADER_LENGTH - 1] = {0};
    char active_if_name[IFNAMSIZ] = {0}; // 활성 인터페이스 이름 저장 버퍼

    // 고유 ID 로드
    if (!load_unique_id(unique_id)) {
        printf("Unique ID not found or malformed in %s. Please run client_0001 first to get an ID.\n", UNIQUE_ID_FILE);
        exit(EXIT_FAILURE);
    }
    printf("Loaded unique ID: %s\n", unique_id);

    // 활성 인터페이스 자동 감지
    if (!find_active_interface_name(active_if_name, sizeof(active_if_name))) {
        fprintf(stderr, "Error: No active non-loopback network interface found with an IP address.\n");
        exit(EXIT_FAILURE);
    }
    printf("Detected active interface for ARP scan: %s\n", active_if_name);


    // ARP 스캔 및 테이블 수집 (자동 감지된 인터페이스 사용)
    perform_arp_scan_and_collect_table(active_if_name, arp_table_collected, sizeof(arp_table_collected));
    printf("Collected ARP Table: %s\n", arp_table_collected);

    // 이제 이 ARP 테이블 데이터를 TCP로 서버에 전송
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

    // 최종 데이터 형식: "0002<8-digit-ID><0005><IP:MAC;IP2:MAC2;...>"
    snprintf(final_buffer_tcp, BUFFER_SIZE, "%s%s%s%s", "0002", unique_id, "0005", arp_table_collected);
    printf("Sending final ARP table data to server (TCP): %s (Length: %zu)\n", final_buffer_tcp, strlen(final_buffer_tcp));

    // 서버로 TCP 데이터 전송
    if (send(final_tcp_sock, final_buffer_tcp, strlen(final_buffer_tcp), 0) < 0) {
        perror("Final TCP send failed");
    } else {
        printf("ARP table data sent successfully via TCP to server.\n");
    }

    close(final_tcp_sock);
    return 0;
}