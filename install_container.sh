#!/bin/bash

if [ -z "$1" ]; then
  echo "Provide container: docker (rootfull) or podman (rootless)"
  exit
elif [ "$1" = "docker" ]; then
  container="docker"
  echo "Installing docker.."
elif [ "$1" = "podman" ]; then
  container="podman"
  echo "Installing podman.."
else 
  echo "Wrong container: provide docker or podman"
  exit
fi


if [ "${container}" = "docker" ]; then

  echo "==================  Install Docker container (you can skip if already installed)=================="

  sudo apt-get update
  sudo apt-get -y install \
      apt-transport-https \
      ca-certificates \
      curl \
      gnupg \
      lsb-release \
      tar 

  # Add Docker’s official GPG key
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg

  echo \
    "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] https://download.docker.com/linux/ubuntu \
    $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

  sudo apt-get update
  sudo apt-get -y install docker-ce docker-ce-cli containerd.io

  sudo usermod -aG docker $USER

  su - $USER

else 

echo "==================  Install Podman container (you can skip if already installed)=================="

sudo apt-get update 
sudo apt-get -y install podman
su - $USER

fi 