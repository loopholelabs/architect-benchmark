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
#include <pthread.h>

#include "bench.h"

int DATA_SIZE_GB = 10;
int TEST_DURATION_S = 10;

void *DATA;
unsigned long DATA_SIZE;

unsigned long *RESULTS;
unsigned long RESULTS_I = 0;
pthread_mutex_t RESULTS_LOCK;

volatile sig_atomic_t PROCEED = 0;

// usage prints the usage message.
void usage()
{
	printf("Usage:\n"
	       "  bech <gigabytes to load>>\n"
	       "  bech <gigabytes to load> <seconds to run>\n");
}

// read_args reads configuration values from command line arguments.
int read_args(int argc, char **argv)
{
	switch (argc) {
	case 1:
		return EXIT_SUCCESS;
	case 2:
		DATA_SIZE_GB = atoi(argv[1]);
		break;
	case 3:
		DATA_SIZE_GB = atoi(argv[1]);
		TEST_DURATION_S = atoi(argv[2]);
		break;
	default:
		printf("Invalid number of arguments.\n");
		usage();
		return EXIT_FAILURE;
	}

	if (DATA_SIZE_GB < 1) {
		printf("Must load at least one gigabyte.\n");
		usage();
		return EXIT_FAILURE;
	}
	if (TEST_DURATION_S < 1) {
		printf("Must run for more than one second.\n");
		usage();
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
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
	if (offset + size > DATA_SIZE_GB * GB) {
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

// sum returns the sum of the n first values from data.
unsigned long long sum(unsigned long *data, unsigned long n)
{
	unsigned long long sum = 0;
	for (unsigned long i = 0; i < n; i++) {
		sum += data[i];
	}
	return sum;
}

// percentile returns the k-th percentile of the first n values of data.
float percentile(unsigned long *data, unsigned long n, int k)
{
	unsigned long r = (k * (n - 1)) / 100;
	unsigned long rmod = (k * (n - 1)) % 100;
	if (rmod == 0)
		return data[r];
	return data[r] + (rmod / 100.0) * (data[r + 1] - data[r]);
}

// stdev returns the standard deviation of the first n values of data given its
// average avg.
float stdev(unsigned long *data, unsigned long n, float avg)
{
	double sqrdiff = 0;
	for (unsigned long i = 0; i < n; i++) {
		double diff = data[i] - avg;
		sqrdiff += diff * diff;
	}
	return sqrt(sqrdiff / n);
}

int main(int argc, char **argv)
{
	int ret = EXIT_SUCCESS;

	if (read_args(argc, argv) != EXIT_SUCCESS) {
		exit(EXIT_FAILURE);
	}
	DATA_SIZE = DATA_SIZE_GB * GB;

	struct timespec clock_res;
	clock_getres(CLOCK_MONOTONIC, &clock_res);
	printf("Clock resolution is %ld ns.\n", clock_res.tv_nsec);

	// Initialize RNG seed, RESULTS_LOCK mutex, and signal handler.
	srand(time(NULL));
	pthread_mutex_init(&RESULTS_LOCK, NULL);
	RESULTS = (unsigned long *)calloc(sizeof(unsigned long), RESULTS_MAX);

	signal(SIGUSR1, handle_signal);
	sigset_t set, old_set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);

	printf("Loading %d GB into memory...\n", DATA_SIZE_GB);
	DATA = (void *)malloc(DATA_SIZE);
	unsigned long loaded = load_mem();
	if (loaded == 0) {
		ret = EXIT_FAILURE;
		goto free;
	}
	printf("Loaded %ld GB into memory.\n", loaded / GB);

	printf("Waiting for SIGUSR1...\n");
	sigprocmask(SIG_BLOCK, &set, &old_set);
	while (!PROCEED)
		sigsuspend(&old_set);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	printf("Signal received.\n");

	printf("Reading memory for %ds...\n", TEST_DURATION_S);
	pthread_t tids[WAIT_THREADS];
	int total_runs = TEST_DURATION_S * 1000 / READ_INTERVAL_MS;
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
	qsort(RESULTS, RESULTS_I, sizeof(unsigned long), cmpulong);
	unsigned long long s = sum(RESULTS, RESULTS_I);
	float avg = s / (float)RESULTS_I;
	float st = stdev(RESULTS, RESULTS_I, avg);
	float p99 = percentile(RESULTS, RESULTS_I, 99);
	float p95 = percentile(RESULTS, RESULTS_I, 95);
	float p90 = percentile(RESULTS, RESULTS_I, 90);

	printf("Results:\n");
	printf("  Min: %ld ns\n", RESULTS[0]);
	printf("  Max: %ld ns\n", RESULTS[RESULTS_I - 1]);
	printf("  Sum: %lld ns\n", s);
	printf("  Avg: %.2f ns\n", avg);
	printf("  Stdev: %.2f ns\n", st);
	printf("  P99: %.2f ns\n", p99);
	printf("  P95: %.2f ns\n", p95);
	printf("  P90: %.2f ns\n", p90);

free:
	free(DATA);
	free(RESULTS);
	exit(ret);
}
