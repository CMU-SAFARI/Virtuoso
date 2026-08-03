for node in kratos{0,1,3,5,6,7,8,9}; do srun -N1 -w $node --exclusive --ntasks=1 bash -c 'mkdir -p /mnt/local/vlnitu/vmorph_results/'; done

