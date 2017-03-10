#!/bin/bash

for DATASTRUCTURE in capq cadm cain catree linden spraylist klsm1024 klsm65536 globallock multiqC16
do
	python bench_file_shortest_path.py 1 $DATASTRUCTURE
done
