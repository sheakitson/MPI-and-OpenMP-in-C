#!/bin/bash

# Gives a name for the job
#SBATCH --job-name=OMP

# Ask the scheduler for Y CPU cores on the same compute node
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4

# Set the name of the output file
#SBATCH -o OMP.out

gcc -fopenmp -O2 -o project_OMP project_OMP.c
rm -f inputs
ln -s $1 inputs
time ./project_OMP large_inputs
sort -k 1,1n -k 2,2n -k 3,3n result_OMP.txt > sorted_OMP.txt
