FROM ubuntu:24.04

WORKDIR /src
RUN apt update && \
  apt install -y build-essential libnuma-dev
COPY . .
RUN make

ENTRYPOINT ["./bench"]
