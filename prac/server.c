#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define OFFICE_DIR "/var/www/html/office"
#define CHILE_DIR "/var/www/html/office/chile"

void compile_me(int os, char *unique_id) {
    char src[256], dst[256], cmd[512];
    FILE *fp, *fp_tmp;

    // me.c 또는 me_windows.c 선택
    if (os == 1) {
        sprintf(src, "%s/me_windows.c", CHILE_DIR);
        sprintf(dst, "%s/%s/me.exe", OFFICE_DIR, unique_id);
    } else {
        sprintf(src, "%s/me.c", CHILE_DIR);
        sprintf(dst, "%s/%s/me", OFFICE_DIR, unique_id);
    }

    // 고유 번호 삽입
    fp = fopen(src, "r");
    sprintf(cmd, "/tmp/me_%s.c", unique_id);
    fp_tmp = fopen(cmd, "w");
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "#define UNIQUE_ID")) {
            fprintf(fp_tmp, "#define UNIQUE_ID \"%s\"\n", unique_id);
        } else {
            fputs(line, fp_tmp);
        }
    }
    fclose(fp);
    fclose(fp_tmp);

    // 컴파일
    if (os == 1) {
        sprintf(cmd, "i686-w64-mingw32-gcc /tmp/me_%s.c -o %s -lws2_32", unique_id, dst);
    } else {
        sprintf(cmd, "gcc /tmp/me_%s.c -o %s", unique_id, dst);
    }
    system(cmd);

    // 임시 파일 삭제
    sprintf(cmd, "rm /tmp/me_%s.c", unique_id);
    system(cmd);
}

int main() {
    // CGI 입력 읽기
    char buf[256];
    int len = fread(buf, 1, sizeof(buf) - 1, stdin);
    buf[len] = '\0';
    int os = atoi(buf);

    // 고유 번호 생성
    char unique_id[32];
    sprintf(unique_id, "%ld", time(NULL));

    // 디렉토리 생성
    char dir[256];
    sprintf(dir, "%s/%s", OFFICE_DIR, unique_id);
    mkdir(dir, 0755);

    // me 파일 컴파일
    compile_me(os, unique_id);

    // HTTP 응답
    printf("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%s", unique_id);
    return 0;
}

이 코드를 수정해야해
클라이언트에서 지금은 os에 따라 번호밖에 보내지 않지만 나중에는 여러 프로그램이 정보를 보낼 예정이야.
그래서 클라이언트에서는 실행될 프로그램에 따라 번호를 보내고 바로 뒤에 프로그램의 실행 값을 보낼 예정이야. 예을 들자면 os 정보를 보내는 프로그램이 윈도우라고 정보를 보낼 때는 (프로그램 번호, 정보)=(1, 1) 이런식으로. 따라서 프로그램 번호에 따라 다른 행위를 해야하는데 일단 os번호를 보내는 프로그램은 1 을 쓸 예정이야. 아직 2,3,4 등등은 생각 안했으니 형태만 만들어놓고 비워놔.
클라이언트가 엄청 많을 예정이라 어떻게 처리할지 고민중이야. 내가 생각한 방법은 입력 값을 받을 때 마다 fork 해서 처리하는 방식인데 더 좋은 방법 있으면 얘기해줘