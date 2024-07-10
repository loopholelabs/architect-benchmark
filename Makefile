BIN = bench
CC = gcc
CFLAGS = -Wall -O2
LDLIBS = -lm -lnuma
DOCKER_IMAGE ?= architect-benchmark

all: $(BIN)

$(BIN):

.PHONY: clean
clean:
	rm -f './bench'

.PHONY: docker
docker:
	docker build --tag $(DOCKER_IMAGE) .

.PHONY: docker-run
docker-run:
	docker run -i -t $(DOCKER_IMAGE) -d 1

.PHONY: docker-signal
docker-signal:
	docker kill --signal SIGUSR1 $(shell docker ps \
			--filter 'ancestor=$(DOCKER_IMAGE)' \
			--filter 'status=running' \
			--format '{{.ID}}'\
	)
