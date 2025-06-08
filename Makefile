CC = gcc
# -pthread 启用POSIX线程支持，链接时无需再加-lpthread
# -Wall 开启所有常见警告
# -pedantic 严格遵守标准
# -Isrc 指定头文件搜索路径为src目录
CFLAGS = -pthread -Wall -pedantic -Isrc
LDFLAGS = 
LDLIBS =

# 编译时开启debug模式：make DEBUG=1
ifdef DEBUG
CFLAGS += -g
LDFLAGS += -g
endif

# 测试程序名
TESTS = thrdtest heavy shutdown pausetest
# 为每个测试程序添加前缀：tests/thrdtest tests/heavy tests/shutdown tests/pausetest
TEST_BINS = $(addprefix tests/,$(TESTS))

# 源文件
THREADPOOL_SRC = src/threadpool.c
THREADPOOL_OBJ = src/threadpool.o

# 生成所有目标
TARGETS = $(TEST_BINS) libthreadpool.so libthreadpool.a
# TARGETS = tests/thrdtest tests/heavy tests/shutdown tests/pausetest \
	libthreadpool.so libthreadpool.a

# 默认目标all依赖所有的目标文件，执行make时默认会构建它们
all: $(TARGETS)

# 模式规则：定义如何生成测试程序；$^：所有依赖文件
# 测试程序：依赖自身.o和线程池.o
tests/%: tests/%.o $(THREADPOOL_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# 通用模式规则：.o由对应的.c生成
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# -fPIC 共享库,位置无关代码
src/libthreadpool.o: src/threadpool.c src/threadpool.h
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

libthreadpool.a: $(THREADPOOL_OBJ)
	ar rcs $@ $^

libthreadpool.so: $(THREADPOOL_OBJ)
	$(CC) -shared -fPIC ${CFLAGS} -o $@ $< ${LDLIBS}

# 简写别名 make shared / make static
shared: libthreadpool.so
static: libthreadpool.a

.PHONY: clean all test shared static

# *~ 常见备份文件
clean:
	rm -f $(TARGETS) *~ */*~ */*.o

test: $(TARGETS)
	./tests/shutdown
	./tests/thrdtest
	./tests/heavy
	./test/pausetest

