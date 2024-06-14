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
Benchmark seed:   1718393125

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
Reading memory every 33ms for 10s...
Read 303 segments of memory.
Calculating results...

Data read sizes:
    Min: 538397 bytes
    Max: 104448524 bytes
    Avg: 54401862.46 bytes
  Stdev: 30586031.83 bytes
    P99: 103187622.24 bytes
    P95: 97357087.90 bytes
    P90: 93253651.40 bytes

Data read times:
    Min: 44 ns
    Max: 386 ns
    Avg: 192.54 ns
  Stdev: 25.23 ns
    P99: 220.98 ns
    P95: 204.00 ns
    P90: 202.00 ns
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
  bech [-h] [-t <seconds>] [-d <gigabytes>] [-s <seed>] [-q]

Options:
  -h  Display this help message.
  -t  Time in seconds for how long the test should run [default: 10].
  -d  Amount of data in gigabytes to load into memory [default: 10].
  -s  Seed for the random number generator [default: current timestamp].
  -q  Quick mode, don't wait for SIGUSR1 before starting test.
```
