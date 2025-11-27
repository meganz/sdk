FROM ubuntu:24.04

ENV DEBCONF_NOWARNINGS=yes
ENV DEBIAN_FRONTEND=noninteractive

ARG USER_ID=1000
ARG GROUP_ID=1000

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    python3-pip \
    git \
    gpg \
    gpg-agent \
    openssh-client \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN groupadd -g ${GROUP_ID} jenkins 2>/dev/null || true && \
    useradd -m -u ${USER_ID} -g ${GROUP_ID} jenkins 2>/dev/null || true

COPY requirements.txt ./requirements.txt

RUN pip3 install --no-cache-dir -r requirements.txt --break-system-packages
