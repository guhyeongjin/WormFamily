#ifndef COMMON_HANDLERS_H
#define COMMON_HANDLERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

// 공통 매크로 정의
#define PORT 443
#define MAX_CLIENTS 10005
#define BUFFER_SIZE 2048
#define UNIQUE_ID_LENGTH 8
#define HEADER_LENGTH 4
#define ID_HEADER_LENGTH 4
#define BASE_DIR "/var/www/html/worldmap/client"

// 실행 파일들이 저장된 경로 (실제 서버 경로로 변경 필요)
extern const char *HOME_DIR;

// 공통 유틸리티 함수 선언
void generate_unique_id(char *id_buffer);
void remove_tcp_client(int *client_sockets, int *num_clients, int client_fd_to_remove);
void handle_generic_data(const char *header_num, const char *data, const char *client_ip_str);
int send_file_with_header_to_client(const char *file_path, int client_sock, const char *executable_name); // 파일 전송 함수

#endif // COMMON_HANDLERS_H