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

The program used [`libuv`](https://libuv.org/) to control the timing of
operations. Before building the benchmark binary you must install it in your
system.

### Debian and Ubuntu

```bash
sudo apt update
sudo apt install libuv1-dev
```

### macOS

```bash
brew install libuv
```

Build the `bench` binary.

```bash
make
```

## Running Benchmark

Start the benchmark binary.

```console
$ ./bench
Clock resolution is 1 ns.
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
Reading memory for 10s...
Read 302 segments of memory.
Calculating results...
Results:
  Min: 26 ns
  Max: 168 ns
  Sum: 34522 ns
  Avg: 114.31 ns
  Stdev: 13.38 ns
  P99: 123.00 ns
  P95: 121.00 ns
  P90: 120.00 ns
```
