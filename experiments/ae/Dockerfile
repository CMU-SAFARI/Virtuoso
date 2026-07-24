# =============================================================================
# TRAIL — Artifact-Evaluation reviewer environment.
#
# Builds a clean Ubuntu image with the build toolchain, the Hugging Face CLI and
# matplotlib pre-installed, and the artifact repo already cloned at
# /work/Virtuoso.  The reviewer then runs a single script inside.
#
#   # build the image (no host clone needed — pulls the branch itself):
#   docker build -t trail-ae \
#     "https://github.com/CMU-SAFARI/Virtuoso.git#trail-artifact-release:experiments/ae"
#
#   # run it (mount a host dir so the ~250 GB trace download persists):
#   mkdir -p ae_bundle
#   docker run -it -v "$PWD/ae_bundle":/work/Virtuoso/ae_bundle trail-ae
#
#   # ...then, inside the container:
#   bash experiments/ae/reproduce.sh --skip-deps
# =============================================================================
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

# Build toolchain + libraries lib/sniper links against (same set as
# experiments/ae/lib/install_deps.sh) plus git / python / pip.
RUN apt-get update && apt-get install -y --no-install-recommends \
        git ca-certificates wget curl less \
        build-essential g++ make cmake automake autoconf libtool pkg-config \
        libboost-dev libsqlite3-dev zlib1g-dev libbz2-dev liblzma-dev libexpat1-dev \
        python3 python3-pip python3-dev \
    && rm -rf /var/lib/apt/lists/*

# Hugging Face CLI (trace download) + matplotlib (figures).
RUN pip3 install --no-cache-dir -U "huggingface_hub[cli]" matplotlib

# Clone the artifact branch.
ARG REPO_URL=https://github.com/CMU-SAFARI/Virtuoso.git
ARG BRANCH=trail-artifact-release
RUN git clone --depth 1 --branch ${BRANCH} ${REPO_URL} /work/Virtuoso

WORKDIR /work/Virtuoso
CMD ["/bin/bash"]
