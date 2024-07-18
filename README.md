<br/>
<div align="center">
  <a href="https://loopholelabs.io">
    <img src="https://cdn.loopholelabs.io/loopholelabs/LoopholeLabsLogo.svg" alt="Logo" height="30">
  </a>
  <h3 align="center">
    A helper program used to benchmark Architect.
  </h3>

[![Discord](https://dcbadge.vercel.app/api/server/JYmFhtdPeu?style=flat)](https://loopholelabs.io/discord)
</div>

## Build

Run the following command to build the `bench` binary.

```bash
make
```

## Running Benchmark

Start the benchmark binary.

```console
$ ./bench
Clock resolution: 1 ns
Benchmark seed:   1721168194
Memory operation: Read

Loading 10 GB into memory...
Loaded 10 GB into memory.
Waiting for SIGUSR1...
```

The program will stop and wait for a `SIGUSR1` signal. Send the signal from
another terminal to proceed.

```bash
pkill -SIGUSR1 bench
```

```console
$ ./bench
...
Signal received.
[102256] Accessing memory every 33ms for 10s...
[102256] Accessed 303 segments of memory.
[102256] Calculating results...
[102256] Data sample sizes:
[102256]     Min: 0.037 MB
[102256]     Max: 9.988 MB
[102256]     Avg: 4.855 MB
[102256]   Stdev: 2.802 MB
[102256]     P99: 9.935 MB
[102256]     P95: 9.478 MB
[102256]     P90: 8.840 MB
[102256] Data operation times:
[102256]     Min: 103 ns
[102256]     Max: 967 ns
[102256]     Avg: 493.88 ns
[102256]   Stdev: 106.78 ns
[102256]     P99: 852.56 ns
[102256]     P95: 689.30 ns
[102256]     P90: 621.60 ns
[102256] Data operation throughput:
[102256]     Min: 97.002 GB/s
[102256]     Max: 43450.945 GB/s
[102256]     Avg: 10018.203 GB/s
[102256]   Stdev: 5414.065 GB/s
[102256]     P99: 400.824 GB/s
[102256]     P95: 1307.051 GB/s
[102256]     P90: 2912.875 GB/s
```

### Running with Docker

Alternatively, you can run the benchmark as a Docker container. The
[`Makefile`](Makefile) provides some helper targets.

Build the OCI image.

```bash
make docker
```

Run the test container.

```console
$ make docker-run
docker run -i -t architect-benchmark -d 1
Clock resolution: 1000000 ns
Benchmark seed:   1718394201

Loading 1 GB into memory...
Loaded 1 GB into memory.
Waiting for SIGUSR1...
```

From another terminal, send a `SIGUSR1` signal to the test container.

```bash
make docker-signal
```

## Usage

```
Architect Memory Benchmark.

Usage:
  bech [-h] [-t <seconds>] [-d <gigabytes>] [-s <seed>] [-r <path>] [-f <number>] [-n] [-w] [-q]

Options:
  -h  Display this help message.
  -t  Time in seconds for how long the test should run [default: 10].
  -d  Amount of data in gigabytes to load into memory [default: 10].
  -s  Seed for the random number generator [default: current timestamp].
  -f  Number of processes to forks for memory access [default: 1].
  -n  If set, distribute forked processes across NUMA nodes.
  -r  Path used to indicate the benchmark is ready to run.
  -w  Measure memory writes instead of reads.
  -q  Quick mode, don't wait for SIGUSR1 before starting test.
```
