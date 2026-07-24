# =============================================================================
# TRAIL — Artifact-Evaluation clean-room environment.
#
# A bare, pinned Ubuntu base. Nothing is pre-installed and the repo is NOT
# cloned — you run every step yourself, so the whole setup is visible.
#
#   # build the tiny base image and enter it (mount a host dir for the traces):
#   docker build -t trail-ae \
#     "https://github.com/CMU-SAFARI/Virtuoso.git#trail-artifact-release:experiments/ae"
#   mkdir -p ae_bundle
#   docker run -it -v "$PWD/ae_bundle":/work/ae_bundle trail-ae
#
#   # ...then, inside the container:
#   apt-get update && apt-get install -y git
#   git clone --branch trail-artifact-release https://github.com/CMU-SAFARI/Virtuoso.git
#   cd Virtuoso
#   bash experiments/ae/lib/install_deps.sh          # apt toolchain + hf CLI + matplotlib
#   bash experiments/ae/reproduce.sh --skip-deps     # build + download + validate
# =============================================================================
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /work
CMD ["/bin/bash"]
