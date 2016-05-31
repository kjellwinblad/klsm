#!/bin/bash

for DATASTRUCTURE in adaptiverelaxedqdcatree multiq relaxedqdcatree qdcatree spraylist linden globallock dlsm klsm klsm16 klsm128 klsm256 klsm4096
do
	python3.4 bench_file_shortest_path.py 1 $DATASTRUCTURE
done
