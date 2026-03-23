

BPFT=/usr/lib/linux-tools-5.15.0-153/bpftool

all: instrument pf_energy minor_fault_stress

instrument: instrument.bpf.c instrument.c
	clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -I. -c instrument.bpf.c -o instrument.bpf.o
	$(BPFT) gen skeleton instrument.bpf.o > instrument.skel.h
	cc -O2 -g -Wall -Wextra -I. instrument.c -o instrument -lelf -lbpf -lz


pf_energy: pf_energy.c
	gcc -O2 -Wall -std=c11 pf_energy.c -o pf_energy


minor_fault_stress: minor_fault_stress.c 
	gcc -O2 -Wall -std=c11 minor_fault_stress.c -o minor_fault_stress
	
clean:
	rm -f instrument pf_energy minor_fault_stress instrument.bpf.o 