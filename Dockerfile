FROM ubuntu:24.04

WORKDIR /src
RUN apt update && \
  apt install -y build-essential
COPY . .
RUN make

ENTRYPOINT ["./bench"]
