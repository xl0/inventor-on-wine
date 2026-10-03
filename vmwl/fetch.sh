#!/usr/bin/env bash
# Download the Ubuntu 26.04 cloud image into vmwl/dl/ and verify it: GPG signature of SHA256SUMS
# (host's ubuntu-cloudimage-keyring), then the image's sha256.
set -euo pipefail
cd "$(dirname "$0")"; mkdir -p dl; cd dl
U=https://cloud-images.ubuntu.com/releases/26.04/release
I=ubuntu-26.04-server-cloudimg-amd64.img
curl -fsSL -O $U/SHA256SUMS -O $U/SHA256SUMS.gpg
gpgv --keyring /usr/share/keyrings/ubuntu-cloudimage-keyring.gpg SHA256SUMS.gpg SHA256SUMS
[ -f $I ] || curl -fSL -o $I $U/$I
grep " \*$I\$" SHA256SUMS | sed 's/ \*/  /' | sha256sum -c -
