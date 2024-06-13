FROM ubuntu:24.04

WORKDIR /src
RUN apt update && \
  apt install -y build-essential libuv1-dev
COPY . .
RUN make

ENTRYPOINT ["./bench"]
