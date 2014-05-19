#A tool to take [ms](http://home.uchicago.edu/rhudson1/source/mksamples.html) output and create a PED/MAP file pair for use with PLINK.

#Example use:  make 3,000 cases + 3,000 controls and 500 SNPs.  That is 6,000 diploids, so 12,000 chromosomes.  Using ms:

```{sh}
ms 12000 1 -s 500 | ./ms2plink fake.ped fake.map
```

##Then, to get $10^6$ permutations:

```{sh}
#We use --map3 b/c we do not have the 3rd column in the map file, which is position in cM.
plink --file fake --assoc --map3 --mperm 1000000 --mperm-save-all
```


Full example of a workflow:

```{sh}
#Simulate some data.
ms 12000 1 -s 500 | ./ms2plink fake.ped fake.map
#Make binary input files
../../plink_test/plink-1.07-srcKRT/plink --file fake --make-bed --map3 --out fake
#Do perms
../../plink_test/plink-1.07-srcKRT/plink --bfile fake --assoc --map3 --mperm 200000 --mperm-save-all --hwe 1e-6 --out fake
```

The above are executed in the script called fakeit.sh.


#Requirements on HPC:

```
module load krthornt/libsequence/1.8.0
```

