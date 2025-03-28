# CS534-Final-Project

## Binary Benchmarks for ARM 32-bit Architecture

Benchmarks used are sourced from MiBench https://vhosts.eecs.umich.edu/mibench/ and https://github.com/embecosm/mibench.  Already compiled binaries are located in the ```bin``` folder which include basicmath, qsort, dijkstra and fft. If needed to compile other benchmarks, steps are listed below. 

### 1. Install the ARM Cross-Compiler

To compile programs for ARM 32-bit, you need to install the **ARM cross-compiler**. This toolchain allows you to generate binaries that will run on ARM-based devices from a host machine.

To install the ARM GCC toolchain, run the following command:

```bash
sudo apt-get install gcc-arm-linux-gnueabihf
```

### 2. Clone Repository and Makefile Modification
Clone the MiBench Repo 
``` bash 
git clone https://github.com/embecosm/mibench
```
Next to compile the benchmarks for ARM 32-bit (AArch32) architecture, you will need to modify the Makefile to use the ARM cross-compiler (arm-linux-gnueabihf-gcc). Below is an example of how to modify the Makefile for an ARM 32-bit target. This example uses a simple sorting program (qsort_small and qsort_large) for demonstration.

Example Makefile for ARM 32-bit:
```bash
CC = arm-linux-gnueabihf-gcc   # Use the ARM cross-compiler

FILE1 = qsort_small.c          # Source file for the small benchmark
FILE2 = qsort_large.c          # Source file for the large benchmark

# Default target, compiles both small and large benchmarks
all: qsort_small qsort_large

# Rule to compile the small benchmark
qsort_small: qsort_small.c Makefile
	$(CC) -static qsort_small.c -O3 -o qsort_small -lm

# Rule to compile the large benchmark
qsort_large: qsort_large.c Makefile
	$(CC) -static qsort_large.c -O3 -o qsort_large -lm

# Clean up generated files
clean:
	rm -rf qsort_small qsort_large output*
```

### 3. Compile the Benchmark

After modifying the Makefile to use the ARM cross-compiler, you can compile the benchmark of your choice. In this case, the qsort_small and qsort_large benchmarks will be compiled for ARM 32-bit. Run the following command:
```bash
make
```
This will trigger the compilation process. The output binaries (qsort_small and qsort_large) will be created in the current directory. Already compiled binaries are located in the ```bin``` folder

## Running Binary in gem5
Assumed dependencies are installed. 
### 1. Build ISA
More information can be found here https://www.gem5.org/documentation/general_docs/building
```bash
scons build/ARM/gem5.fast -j 4
```
### 2. Running Provided Configuration Script
After build, run system emulation configuration script with these command line arguments:
```bash
build/ARM/gem5.fast --outdir=m5out/ configs/deprecated/example/se.py \
 --cpu-type=ArmMinorCPU \
--cpu-clock=4GHz \
--cacheline_size=64 \
--caches \
--l1i_size=8kB \
--l1d_size=8kB \
--l2cache \
--l2_size=4MB \
--l2_assoc=16 \
--num-cpus=4 \
--cmd=path/to/bin/basicmath_large
```
Cacheline sizes and CPU frequency to be changed to model edge device.
