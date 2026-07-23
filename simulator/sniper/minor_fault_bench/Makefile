

CXX = gcc
CXXFLAGS = -g -O2 -Wall -Wextra
LDFLAGS = -lpthread -lrt


all: minor_fault_microbench hello_riscv

minor_fault_microbench: minor_fault_microbench.c
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

hello_riscv: hello.c
	/opt/riscv/toolchain/bin/riscv64-unknown-elf-gcc $(CXXFLAGS) -o $@ $^ 