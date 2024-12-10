FROM ubuntu:24.04

ENV DEBCONF_NOWARNINGS=yes
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    python3-pip \
    git \
    gpg \
    gpg-agent \
    openssh-client \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
    
COPY requirements.txt ./requirements.txt

RUN pip3 install --no-cache-dir -r requirements.txt --break-system-packages
