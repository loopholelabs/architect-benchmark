/*
	Copyright 2024 Loophole Labs

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

		   http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/wait.h>

#include "bench.h"

void *DATA;
unsigned long DATA_SIZE;

unsigned long *SAMPLES;
unsigned long *RESULTS;
unsigned long RESULTS_SIZE;
unsigned long RESULTS_I = 0;
pthread_mutex_t TICK_LOCK;
pthread_cond_t TICK;

volatile sig_atomic_t PROCEED = 0;

enum MemOp {
	READ,
	WRITE,
};

static const char *MEM_OP_STRING[] = {
	"Read",
	"Write",
};

// stats are statistics values computed from sampled data.
struct stats {
	unsigned long min;
	unsigned long max;
	double avg;
	double stdev;
	double p99;
	double p95;
	double p90;
};

// benchmark_opts are the options used to customize a benchmark run.
struct benchmark_opts {
	int duration;
	int data_size;
	long seed;
	bool quick;
	bool fork;
	enum MemOp mem_op;
	char *ready_file;
};

// usage prints the usage message.
void usage()
{
	printf("Architect Memory Benchmark.\n\n"
	       "Usage:\n"
	       "  bech [-h] [-t <seconds>] [-d <gigabytes>] [-s <seed>] [-r <path>] [-f] [-w] [-q]\n"
	       "\nOptions:\n"
	       "  -h  Display this help message.\n"
	       "  -t  Time in seconds for how long the test should run [default: 10].\n"
	       "  -d  Amount of data in gigabytes to load into memory [default: 10].\n"
	       "  -s  Seed for the random number generator [default: current timestamp].\n"
	       "  -r  Path used to indicate the benchmark is ready to run.\n"
	       "  -f  Forks a child process to run memory access.\n"
	       "  -w  Measure memory writes instead of reads.\n"
	       "  -q  Quick mode, don't wait for SIGUSR1 before starting test.\n");
}

// load_mem reads DATA_SIZE bytes of random data into DATA.
unsigned long load_mem()
{
	int rand_fd = open("/dev/urandom", O_RDONLY);
	if (rand_fd == -1) {
		printf("Failed to open /dev/urandom: %s\n", strerror(errno));
		return 0;
	}

	unsigned long loaded = 0;
	while (loaded < DATA_SIZE) {
		int got = read(rand_fd, DATA + loaded, GB);
		if (got == -1) {
			printf("Failed to load data into memory: %s\n",
			       strerror(errno));
			return 0;
		}
		loaded += got;
	}

	return loaded;
}

// access_mem reads or writes a random chunk of DATA and stores how much data
// was used in SAMPLES and how long the operation took in RESULTS.
static void *access_mem(void *arg)
{
	enum MemOp *mem_op = (enum MemOp *)arg;

	// Notify main thread when ready to handle ticks.
	pthread_mutex_lock(&TICK_LOCK);
	pthread_cond_signal(&TICK);
	pthread_mutex_unlock(&TICK_LOCK);

	while (true) {
		pthread_mutex_lock(&TICK_LOCK);
		pthread_cond_wait(&TICK, &TICK_LOCK);

		unsigned long size = rand() % (MEM_OP_MAX_MB * MB);
		unsigned long offset = rand();
		offset = (offset << 12 | rand()) % (DATA_SIZE - 1);

		// Adjust how much data to manipulate to make sure we stay within
		// bounds.
		if (offset + size > DATA_SIZE) {
			size = DATA_SIZE - offset;
		}

		void *buf = malloc(size);
		memset(buf, 0, size);

		// Read or write from DATA and track how long the operation takes.
		struct timespec before, after;
		clock_gettime(CLOCK_MONOTONIC, &before);
		switch (*mem_op) {
		case READ:
			memcpy(buf, DATA + offset, size);
			break;
		case WRITE:
			memcpy(DATA + offset, buf, size);
			break;
		}
		clock_gettime(CLOCK_MONOTONIC, &after);

		free(buf);

		// Store time elapsed in nanoseconds.
		long secs_diff = after.tv_sec - before.tv_sec;
		long nsecs_diff = after.tv_nsec - before.tv_nsec;
		long diff = secs_diff * 1000000000 + nsecs_diff;

		if (RESULTS_I < RESULTS_SIZE) {
			SAMPLES[RESULTS_I] = size;
			RESULTS[RESULTS_I] = diff;
			RESULTS_I++;
		} else {
			printf("WARN: Result storage limit reached.\n");
		}
		pthread_mutex_unlock(&TICK_LOCK);
	}
	return NULL;
}

// handle_signal unblocks the process to continue.
void handle_signal(int sig)
{
	PROCEED = 1;
	signal(sig, handle_signal);
}

// cmpulong compares two unsigned long values.
int cmpulong(const void *a, const void *b)
{
	const unsigned long aul = *(unsigned long *)a;
	const unsigned long bul = *(unsigned long *)b;

	if (aul < bul)
		return -1;
	else if (aul > bul)
		return 1;
	return 0;
}

// percentile returns the k-th percentile of the first n values of data.
double percentile(unsigned long *data, unsigned long n, int k)
{
	unsigned long r = (k * (n - 1)) / 100;
	unsigned long rmod = (k * (n - 1)) % 100;
	if (rmod == 0)
		return data[r];
	return data[r] + (rmod / 100.0) * (data[r + 1] - data[r]);
}

// compute_stats calculates statistics about data. It assumes data is sorted in
// ascending order.
void compute_stats(struct stats *res, unsigned long *data, unsigned long size)
{
	// Handle trivial cases.
	switch (size) {
	case 0:
		res->min = 0;
		res->max = 0;
		res->avg = 0;
		res->stdev = 0;
		res->p99 = 0;
		res->p95 = 0;
		res->p90 = 0;
		return;
	case 1:
		res->min = data[0];
		res->max = data[0];
		res->avg = data[0];
		res->stdev = 0;
		res->p99 = data[0];
		res->p95 = data[0];
		res->p90 = data[0];
		return;
	}

	double avg = data[0], prev_avg = data[0];
	double var = 0;

	for (unsigned long i = 1; i < size; i++) {
		avg = prev_avg + (data[i] - prev_avg) / i;
		var += (data[i] - prev_avg) * (data[i] - avg);
		prev_avg = avg;
	}

	res->min = data[0];
	res->max = data[size - 1];
	res->avg = avg;
	res->stdev = sqrt(var / (size - 1));
	res->p99 = percentile(data, size, 99);
	res->p95 = percentile(data, size, 95);
	res->p90 = percentile(data, size, 90);
}

int benchmark(struct benchmark_opts opts)
{
	int ret = EXIT_SUCCESS;

	struct timespec clock_res;
	clock_getres(CLOCK_MONOTONIC, &clock_res);
	printf("Clock resolution: %ld ns\n", clock_res.tv_nsec);
	printf("Benchmark seed:   %ld\n", opts.seed);
	printf("Memory operation: %s\n", MEM_OP_STRING[opts.mem_op]);
	printf("\n");

	// Initialize RNG seed, signal handler, and shared variables.
	srand(opts.seed);
	pthread_mutex_init(&TICK_LOCK, NULL);
	pthread_cond_init(&TICK, NULL);

	DATA_SIZE = opts.data_size * GB;
	DATA = (void *)malloc(DATA_SIZE);
	RESULTS_SIZE = opts.duration * 1000 / TICK_INTERVAL_MS;
	SAMPLES = (unsigned long *)calloc(sizeof(unsigned long), RESULTS_SIZE);
	RESULTS = (unsigned long *)calloc(sizeof(unsigned long), RESULTS_SIZE);
	struct stats *results_stats = calloc(sizeof(struct stats), 1);
	struct stats *samples_stats = calloc(sizeof(struct stats), 1);

	signal(SIGUSR1, handle_signal);
	sigset_t set, old_set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);

	printf("Loading %d GB into memory...\n", opts.data_size);
	unsigned long loaded = load_mem();
	if (loaded == 0) {
		ret = EXIT_FAILURE;
		goto free;
	}
	printf("Loaded %ld GB into memory.\n", loaded / GB);

	if (opts.ready_file != NULL) {
		if (remove(opts.ready_file) && errno != ENOENT) {
			printf("Failed to delete ready file: %s\n",
			       strerror(errno));
			goto free;
		}

		printf("Creating ready file %s...\n", opts.ready_file);
		FILE *fp = fopen(opts.ready_file, "ab+");
		if (fp == NULL) {
			printf("Failed to create ready file: %s\n",
			       strerror(errno));
			goto free;
		}
		fclose(fp);
	}
	if (!opts.quick) {
		printf("Waiting for SIGUSR1...\n");
		sigprocmask(SIG_BLOCK, &set, &old_set);
		while (!PROCEED)
			sigsuspend(&old_set);
		sigprocmask(SIG_UNBLOCK, &set, NULL);
		printf("Signal received.\n");
	}

	if (opts.fork) {
		printf("Forking child process...\n");
		pid_t pid = fork();
		if (pid != 0) {
			waitpid(pid, NULL, 0);
			goto free;
		}
	}

	printf("Accessing memory every %dms for %ds...\n", TICK_INTERVAL_MS,
	       opts.duration);
	struct timespec tick_interval = {
		.tv_sec = TICK_INTERVAL_MS / 1000,
		.tv_nsec = (TICK_INTERVAL_MS % 1000) * 1000000,
	};

	pthread_t mem_op_tid;
	pthread_create(&mem_op_tid, NULL, access_mem, (void *)&opts.mem_op);

	// Wait for the background thread to be ready to handle ticks.
	pthread_mutex_lock(&TICK_LOCK);
	pthread_cond_wait(&TICK, &TICK_LOCK);
	pthread_mutex_unlock(&TICK_LOCK);

	for (int i = 0; i < RESULTS_SIZE; i++) {
		int lock_res = pthread_mutex_trylock(&TICK_LOCK);
		if (lock_res == 0) {
			pthread_cond_signal(&TICK);
			pthread_mutex_unlock(&TICK_LOCK);
		} else {
			printf("WARN: Lock is busy, missing tick.\n");
		}
		if (nanosleep(&tick_interval, NULL))
			break;
	}

	pthread_cancel(mem_op_tid);
	pthread_join(mem_op_tid, NULL);
	printf("Accessed %ld segments of memory.\n", RESULTS_I);
	if (RESULTS_I == 0) {
		goto free;
	}

	printf("Calculating results...\n");
	qsort(SAMPLES, RESULTS_I, sizeof(unsigned long), cmpulong);
	qsort(RESULTS, RESULTS_I, sizeof(unsigned long), cmpulong);

	compute_stats(samples_stats, SAMPLES, RESULTS_I);
	printf("\nData sample sizes:\n");
	printf("    Min: %.3f MB\n", samples_stats->min / (double)MB);
	printf("    Max: %.3f MB\n", samples_stats->max / (double)MB);
	printf("    Avg: %.3f MB\n", samples_stats->avg / MB);
	printf("  Stdev: %.3f MB\n", samples_stats->stdev / MB);
	printf("    P99: %.3f MB\n", samples_stats->p99 / MB);
	printf("    P95: %.3f MB\n", samples_stats->p95 / MB);
	printf("    P90: %.3f MB\n", samples_stats->p90 / MB);

	compute_stats(results_stats, RESULTS, RESULTS_I);
	printf("\nData operation times:\n");
	printf("    Min: %ld ns\n", results_stats->min);
	printf("    Max: %ld ns\n", results_stats->max);
	printf("    Avg: %.2f ns\n", results_stats->avg);
	printf("  Stdev: %.2f ns\n", results_stats->stdev);
	printf("    P99: %.2f ns\n", results_stats->p99);
	printf("    P95: %.2f ns\n", results_stats->p95);
	printf("    P90: %.2f ns\n", results_stats->p90);

free:
	if (opts.ready_file != NULL)
		remove(opts.ready_file);
	free(samples_stats);
	free(results_stats);
	free(DATA);
	free(RESULTS);
	pthread_cond_destroy(&TICK);
	pthread_mutex_destroy(&TICK_LOCK);

	exit(ret);
}

int main(int argc, char **argv)
{
	int opt;
	int data_size = 10;
	int test_duration = 10;
	long seed = time(0);
	bool quick = false, use_fork = false;
	enum MemOp mem_op = READ;
	char *ready_file = NULL;

	while ((opt = getopt(argc, argv, "t:d:s:r:wqfh")) != -1) {
		switch (opt) {
		case 't':
			test_duration = atoi(optarg);
			break;
		case 'd':
			data_size = atoi(optarg);
			break;
		case 's':
			seed = atol(optarg);
			break;
		case 'q':
			quick = true;
			break;
		case 'f':
			use_fork = true;
			break;
		case 'r':
			ready_file = optarg;
			break;
		case 'w':
			mem_op = WRITE;
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		default:
			usage();
			exit(EXIT_FAILURE);
		}
	}

	if (data_size < 1) {
		printf("Must load at least one gigabyte.\n");
		usage();
		exit(EXIT_FAILURE);
	}
	if (test_duration < 1) {
		printf("Must run for more than one second.\n");
		usage();
		exit(EXIT_FAILURE);
	}
	if (seed < 1) {
		printf("Invalid benchmark seed.\n");
		usage();
		exit(EXIT_FAILURE);
	}

	struct benchmark_opts opts = {
		.duration = test_duration,
		.data_size = data_size,
		.seed = seed,
		.quick = quick,
		.fork = use_fork,
		.mem_op = mem_op,
		.ready_file = ready_file,
	};
	return benchmark(opts);
}
