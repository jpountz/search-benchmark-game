FROM gcc:12 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y \
    && apt-get install -y --no-install-recommends \
       cmake \
       libtool \
       m4 \
       automake \
       autoconf \
       git \
       ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /home/pisa/src

ENTRYPOINT ["make", "compile-within-container"]
