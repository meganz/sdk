FROM ubuntu:22.04

ENV DEBCONF_NOWARNINGS=yes
ENV DEBIAN_FRONTEND=noninteractive
ENV HOME=/mega

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    python3-pip \
    git \
    gpg \
    gpg-agent \
    && apt-get clean && rm -rf /var/lib/apt/lists/* \
    && useradd mega -d /mega -m -s /bin/bash
    
RUN mkdir -p /mega/.gnupg && \
    chown -R mega:mega /mega/.gnupg && \
    chmod 700 /mega/.gnupg
    
COPY requirements.txt ./requirements.txt

RUN pip3 install --no-cache-dir -r requirements.txt

USER mega
WORKDIR /mega


