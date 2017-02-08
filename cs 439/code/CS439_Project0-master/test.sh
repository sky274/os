#!/bin/bash
# Thanks to Megan Avery for helping to write this script
NUM=$1

make rtest$NUM > rtest$NUM.txt && make test$NUM > test$NUM.txt && diff rtest$NUM.txt test$NUM.txt -a --side-by-side
