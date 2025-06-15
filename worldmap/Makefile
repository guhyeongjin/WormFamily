# Makefile for server_program

# 컴파일러
CC = gcc

# 컴파일러 옵션
# -Wall: 모든 경고 활성화
# -Wextra: 추가 경고 활성화
# -g: 디버깅 정보 포함
# -I.: 현재 디렉토리에서 헤더 파일 찾기 (server.c 기준)
# -Ihandlers: handlers 디렉토리에서 헤더 파일 찾기
CFLAGS = -Wall -Wextra -g -I. -I./handlers

# 링커 옵션 (소켓 프로그래밍을 위한 라이브러리)
LDFLAGS =

# 실행 파일 이름
TARGET = server_program

# 소스 파일 목록
SRCS = \
	server.c \
	common_handlers.c \
	handlers/handler_0001.c \
	handlers/handler_0002.c \
	handlers/handler_0003.c \
	handlers/handler_4444.c

# 오브젝트 파일 목록 (소스 파일과 동일한 이름, .o 확장자)
# $(patsubst %.c,%.o,$(SRCS))는 SRCS의 모든 .c를 .o로 변경
OBJS = $(patsubst %.c,%.o,$(SRCS))

# 기본 타겟: all
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful!"

# .c 파일을 .o 파일로 컴파일하는 규칙
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# 특정 핸들러 .o 파일의 경로를 수정 (이전 Makefile에서 이미 제대로 작동했지만, 명시적으로)
handlers/%.o: handlers/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# clean 타겟: 생성된 오브젝트 파일과 실행 파일 삭제
clean:
	@echo "Cleaning up..."
	rm -f $(OBJS) $(TARGET) handlers/*.o
	@echo "Clean complete."

.PHONY: all clean