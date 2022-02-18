#!/bin/bash

#Gives a name for the job
#SBATCH --job-name=MPI

# Ask the scheduler to run N MPI processes on N compute nodes
#SBATCH --nodes=4
#SBATCH --ntasks=4

# Set the name of the output file
#SBATCH -o MPI.out

# Load mpi module
module add mpi/openmpi

mpicc -O2 -o project_MPI project_MPI.c
rm -f inputs
ln -s $1 inputs
time mpirun -np 4 ./project_MPI large_inputs
sort -k 1,1n -k 2,2n -k 3,3n result_MPI.txt > sorted_MPI.txt
