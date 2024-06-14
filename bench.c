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

#include "bench.h"

void *DATA;
unsigned long DATA_SIZE;

unsigned long *READS;
unsigned long *RESULTS;
unsigned long RESULTS_I = 0;
pthread_mutex_t RESULTS_LOCK;

volatile sig_atomic_t PROCEED = 0;

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

// usage prints the usage message.
void usage()
{
	printf("Architect Memory Benchmark.\n\n"
	       "Usage:\n"
	       "  bech [-h] [-t <seconds>] [-d <gigabytes>] [-s <seed>] [-q]\n"
	       "\nOptions:\n"
	       "  -h  Display this help message.\n"
	       "  -t  Time in seconds for how long the test should run [default: 10].\n"
	       "  -d  Amount of data in gigabytes to load into memory [default: 10].\n"
	       "  -s  Seed for the random number generator [default: current timestamp].\n"
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

// read_mem reads a random chunk of data from DATA and stores how long it took
// to read it in RESULTS.
static void *read_mem()
{
	unsigned long offset = rand() % (DATA_SIZE - 1);
	unsigned long size = rand() % (READ_MAX_MB * MB);

	// Adjust how much data to read to make sure we stay within bounds.
	if (offset + size > DATA_SIZE) {
		size = DATA_SIZE - offset;
	}

	void *buf = malloc(size);

	// Read from DATA and track how long the operation takes.
	struct timespec before, after;
	clock_gettime(CLOCK_MONOTONIC, &before);
	memcpy(buf, DATA, size);
	clock_gettime(CLOCK_MONOTONIC, &after);

	free(buf);

	// Store time elapsed in nanoseconds.
	long secs_diff = after.tv_sec - before.tv_sec;
	long nsecs_diff = after.tv_nsec;
	if (secs_diff == 0) {
		nsecs_diff = after.tv_nsec - before.tv_nsec;
	}
	long diff = secs_diff * 1e9 + nsecs_diff;

	pthread_mutex_lock(&RESULTS_LOCK);
	if (RESULTS_I < RESULTS_MAX) {
		READS[RESULTS_I] = size;
		RESULTS[RESULTS_I] = diff;
		RESULTS_I++;
	} else {
		printf("WARN: Result storage limit reached.");
	}
	pthread_mutex_unlock(&RESULTS_LOCK);
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
	// Handle trivial case.
	if (size == 1) {
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

int benchmark(int test_duration, int data_size, long seed, bool quick)
{
	int ret = EXIT_SUCCESS;

	struct timespec clock_res;
	clock_getres(CLOCK_MONOTONIC, &clock_res);
	printf("Clock resolution: %ld ns\n", clock_res.tv_nsec);
	printf("Benchmark seed:   %ld\n\n", seed);

	// Initialize RNG seed, signal handler, and shared variables.
	srand(seed);
	pthread_mutex_init(&RESULTS_LOCK, NULL);
	DATA_SIZE = data_size * GB;
	DATA = (void *)malloc(DATA_SIZE);
	READS = (unsigned long *)calloc(sizeof(unsigned long), RESULTS_MAX);
	RESULTS = (unsigned long *)calloc(sizeof(unsigned long), RESULTS_MAX);
	struct stats *results_stats = calloc(sizeof(struct stats), 1);
	struct stats *read_stats = calloc(sizeof(struct stats), 1);

	signal(SIGUSR1, handle_signal);
	sigset_t set, old_set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);

	printf("Loading %d GB into memory...\n", data_size);
	unsigned long loaded = load_mem();
	if (loaded == 0) {
		ret = EXIT_FAILURE;
		goto free;
	}
	printf("Loaded %ld GB into memory.\n", loaded / GB);

	if (!quick) {
		printf("Waiting for SIGUSR1...\n");
		sigprocmask(SIG_BLOCK, &set, &old_set);
		while (!PROCEED)
			sigsuspend(&old_set);
		sigprocmask(SIG_UNBLOCK, &set, NULL);
		printf("Signal received.\n");
	}

	printf("Reading memory every %dms for %ds...\n", READ_INTERVAL_MS,
	       test_duration);
	pthread_t tids[WAIT_THREADS];
	int total_runs = test_duration * 1000 / READ_INTERVAL_MS;
	struct timespec read_interval = { .tv_nsec = READ_INTERVAL_MS * 1e6 };

	for (int i = 0; i < total_runs; i++) {
		pthread_create(&tids[i % WAIT_THREADS], NULL, read_mem, NULL);
		if (nanosleep(&read_interval, NULL))
			break;
	}
	for (int i = 0; i < WAIT_THREADS; i++) {
		pthread_join(tids[i], NULL);
	}
	printf("Read %ld segments of memory.\n", RESULTS_I);

	printf("Calculating results...\n");
	qsort(READS, RESULTS_I, sizeof(unsigned long), cmpulong);
	qsort(RESULTS, RESULTS_I, sizeof(unsigned long), cmpulong);

	compute_stats(read_stats, READS, RESULTS_I);
	printf("\nData read sizes:\n");
	printf("    Min: %ld bytes\n", read_stats->min);
	printf("    Max: %ld bytes\n", read_stats->max);
	printf("    Avg: %.2f bytes\n", read_stats->avg);
	printf("  Stdev: %.2f bytes\n", read_stats->stdev);
	printf("    P99: %.2f bytes\n", read_stats->p99);
	printf("    P95: %.2f bytes\n", read_stats->p95);
	printf("    P90: %.2f bytes\n", read_stats->p90);

	compute_stats(results_stats, RESULTS, RESULTS_I);
	printf("\nData read times:\n");
	printf("    Min: %ld ns\n", results_stats->min);
	printf("    Max: %ld ns\n", results_stats->max);
	printf("    Avg: %.2f ns\n", results_stats->avg);
	printf("  Stdev: %.2f ns\n", results_stats->stdev);
	printf("    P99: %.2f ns\n", results_stats->p99);
	printf("    P95: %.2f ns\n", results_stats->p95);
	printf("    P90: %.2f ns\n", results_stats->p90);

free:
	free(read_stats);
	free(results_stats);
	free(DATA);
	free(RESULTS);
	exit(ret);
}

int main(int argc, char **argv)
{
	int opt;
	int data_size = 10;
	int test_duration = 10;
	long seed = time(0);
	bool quick = false;

	while ((opt = getopt(argc, argv, "t:d:s:qh")) != -1) {
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

	return benchmark(test_duration, data_size, seed, quick);
}
