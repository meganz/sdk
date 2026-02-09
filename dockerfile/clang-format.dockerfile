# MEGA clang-format Linter
# mega-docker.artifactory.developers.mega.co.nz:8443/clang-format-sdk:24.04_v18

FROM ubuntu:24.04

ENV DEBCONF_NOWARNINGS=yes
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y git clang-format && \
    useradd clang-format -d /var/lib/clang-format -m -s /bin/bash

COPY clang-format-mr-check.sh /usr/local/bin/clang-format-mr-check
RUN chmod +x /usr/local/bin/clang-format-mr-check

USER clang-format

WORKDIR /var/lib/clang-format
