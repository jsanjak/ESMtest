#!/bin/sh
#$ -q krt,bio,pub64
#$ -N MERGEBIG
#$ -hold_jid PERMBIGDATA

if [ -a bigfake_merged.all.perms.h5 ]
then
    rm bigfake_merged.all.perms.h5
fi

PERM_FILES=$(echo bigfake.*.perms.h5)

#NEED: any python/2.7.* with h5py and numpy
module purge
module load krthornt/thorntonlab/1.0

#merge 10 perms at a time.
python ~/ESMtest/src/h5merge.py -i $PERM_FILES -o bigfake_merged.all.perms.h5 -n 10

