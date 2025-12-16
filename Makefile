CXX ?= g++

# ================= 配置读取与检查逻辑 =================
# 1. 配置文件名
STD_CONFIG_FILE := cxx_std

# 2. 读取配置，如果文件不存在，默认使用 c++17 并发出警告
ifeq ($(wildcard $(STD_CONFIG_FILE)),)
    target_std := c++17
    $(warning "Warning: $(STD_CONFIG_FILE) not found. Defaulting to C++17. Run 'make config' to configure.")
else
    target_std := $(shell cat $(STD_CONFIG_FILE))
endif

# 3. 检查当前编译器是否真的支持这个标准
#    (避免学生手动改了文件写个 c++99 导致编译报错看不懂，或者评测机环境太老)
STD_CHECK := $(shell $(CXX) -std=$(target_std) -x c++ -E /dev/null >/dev/null 2>&1 && echo "ok")

ifneq ($(STD_CHECK),ok)
    $(error "Error: The configured standard '$(target_std)' is NOT supported by the current compiler ($(CXX)). Please run 'make config' or upgrade your compiler.")
endif

# =======================================================

# 使用读取到的标准
CXXFLAGS = -std=$(target_std) -Wall -Wextra -I./include -fPIE

ifdef DEBUG
    CXXFLAGS += -g -O0
else
    CXXFLAGS += -O2
endif

# 上一次的编译参数记录文件
LAST_FLAGS_FILE = .last_cxxflags

# 源文件
BASE_SRCS = $(shell find src/base -name '*.cpp')
STUDENT_SRCS = $(shell find src/student -name '*.cpp')
HEADERS = $(shell find include -name '*.h' -o -name '*.hpp')

# 所有源文件
SRCS = $(BASE_SRCS) $(STUDENT_SRCS)

# 目标文件
OBJS = $(SRCS:.cpp=.o)

# 基础可执行文件
BASE_EXEC = fle_base

# 工具名称
TOOLS = cc ld nm objdump readfle exec disasm ar

#=============================================================================
# 检查当前 CXXFLAGS 与上一次是否不同，如果不同，就删除所有 .o 文件以触发重新编译
CXXFLAGS_PREV := $(shell cat $(LAST_FLAGS_FILE) 2>/dev/null)
ifneq ($(CXXFLAGS_PREV),$(CXXFLAGS))
$(warning "CXXFLAGS changed, forcing full recompile")
$(shell rm -f $(OBJS))
$(shell echo "$(CXXFLAGS)" > $(LAST_FLAGS_FILE))
endif
#=============================================================================

# 默认目标
all: show_info $(TOOLS)

# 显示当前编译信息
show_info:
	@echo "------------------------"
	@echo "Compiler: $$($(CXX) --version | head -n 1)"
	@echo "Standard: $(target_std)"
	@echo "------------------------"

# 编译源文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# 先编译基础可执行文件
$(BASE_EXEC): $(OBJS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) -pie

# 为每个工具创建符号链接
$(TOOLS): $(BASE_EXEC)
	@if [ ! -L $@ ] || [ ! -e $@ ]; then \
		ln -sf $(BASE_EXEC) $@; \
	fi

config:
	python3 configure.py

# 清理编译产物
clean:
	rm -f $(OBJS) $(BASE_EXEC) $(TOOLS)
	rm -rf tests/cases/*/build
	rm -f $(LAST_FLAGS_FILE)

# 运行测试
test: all
	python3 grader.py

test_1: all
	python3 grader.py --group nm

test_2: all
	python3 grader.py --group basic_linking

test_3: all
	python3 grader.py --group relative_reloc

test_4: all
	python3 grader.py --group symbol_resolution

test_5: all
	python3 grader.py --group addr64

test_6: all
	python3 grader.py --group section_perm

test_8: all
	python3 grader.py --group static_linking

retest: all
	python3 grader.py -f

.PHONY: all clean test show_info test_1 test_2 test_3 test_4 test_5 test_6 test_8 retest config
