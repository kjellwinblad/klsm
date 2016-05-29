#!/bin/bash

for DATASTRUCTURE in adaptiverelaxedqdcatree relaxedqdcatree qdcatree spraylist linden globallock multiq dlsm klsm 
do
	python bench_file_shortest_path.py 1 $DATASTRUCTURE
done
