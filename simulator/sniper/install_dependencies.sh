
#!/bin/bash

# Exit immediately if a command exits with a non-zero status

set -e
path=$(pwd)


echo "Installing python3 system-wide"

sudo apt-get update
sudo apt-get install -y build-essential gcc g++ make zlib1g-dev
sudo apt-get install python3
sudo apt-get install -y python3-pip
sudo apt-get install -y libpython3.10-dev

# These packages are required for plotting
pip3 install pandas numpy matplotlib seaborn 

#if conda is not installed, install it
echo "Conda is not installed. Installing Miniconda..."

# Define Miniconda installer URL (Linux x86_64 version; adjust if needed)
MINICONDA_INSTALLER="https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh"
INSTALL_DIR=$(pwd)/conda

# Download Miniconda installer
echo "Downloading Miniconda installer..."
wget -O miniconda.sh $MINICONDA_INSTALLER

# Run installer
echo "Installing Miniconda..."
bash miniconda.sh -b -p $INSTALL_DIR

# Initialize conda
echo "Initializing conda..."
eval "$($INSTALL_DIR/bin/conda shell.bash hook)"

# Clean up
rm miniconda.sh

echo "Conda installation completed"
