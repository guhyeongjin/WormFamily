
/*
받는 패킷 형식
<HEADER><8-digit-ID><ID_HEADER><payload>
<HEADER> : "헤더" (4 bytes) 전반적인 정보 유형 ex) 0003 = 명령어 실행 결과값
<8-digit-ID> : 고유번호 (8 bytes)
<ID_HEADER> : ID 헤더 (4 bytes) 세부 정보 유형 ex) 0001 = ifconfig, 0002 = route, 0003 = pwd
*/

#include "handler_0003.h" // 헤더 번호 지정


void handle_0003_data(const char *data, const char *client_ip_str) {
    // 데이터 길이 검사
    if (strlen(data) < HEADER_LENGTH + UNIQUE_ID_LENGTH + ID_HEADER_LENGTH) {
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

    // 페이로드 추출 payload
    // 모든 헤더 길이만큼 이동하여 페이로드 시작 위치 계산
    const char *payload = data + HEADER_LENGTH + UNIQUE_ID_LENGTH + ID_HEADER_LENGTH; 

    printf("[0003] Received all network info for ID %s from %s ID_HEADER %s  :  %s\n", unique_id, client_ip_str, id_header, payload);

    // 디렉토리 경로 생성 고유번호 디렉토리
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", BASE_DIR, unique_id);

    if (access(dir_path, F_OK) == -1) {
        if (mkdir(dir_path, 0755) == -1) {
            perror("[0003] Failed to create client directory for network info");
            return;
        }
    }

    // 파일 이름 결정
    // ID 헤더에 따라 파일 이름을 결정
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
    // 최종 파일 경로 file_path
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, file_name);

    FILE *fp = fopen(file_path, "w");
    if (fp == NULL) {
        perror("[0003] Failed to open file");
        return;
    }

    // 페이로드를 세미콜론(;)으로 분리하여 파일에 저장 (아직 안씀)
    /*
    char *temp_payload = strdup(payload);
    if (temp_payload == NULL) { 
        perror("strdup failed"); fclose(fp); return; 
    }

    char *token = strtok(temp_payload, ";");
    while (token != NULL) {
        fprintf(fp, "%s\n", token);
        token = strtok(NULL, ";");
    }
    */
    fprintf(fp, "%s", payload);
    // ;로 분리하는 기능 쓰려면 free (temp_payload);
    fclose(fp);
    printf("[0003] Saved all network info for ID %s to %s.\n", unique_id, file_path);
}