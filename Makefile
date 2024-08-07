
CHECK_PATH=./src  
# ./arm_ap_test

IGNORE_PATH := -path ./import -prune -o
IGNORE_PATH += -path ./.git -prune -o
IGNORE_PATH += -path ./2rdParty -prune -o
IGNORE_PATH += -path ./3rdParty -prune -o
IGNORE_PATH += -path ./build -prune -o
# IGNORE_PATH += -path ./apps/efsmt/att/mem -prune -o

# IGNORE_FILE := ! -iname register_soc.h

format: format-cpp

format-cpp:
	find ${CHECK_PATH} ${IGNORE_PATH}  \
		-iname "*.h" -print -o \
		-iname "*.c" -print -o \
		-iname "*.cc" -print -o \
		-iname "*.cpp" -print | \
		xargs clang-format -i -style=file

