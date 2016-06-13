#!/bin/bash
#linden spraylist qdcatree fpaqdcatree fpaqdcatreenoputadapt fpaqdcatreenormminadapt fpaqdcatreenocatreeadapt klsm2048 klsm4096 klsm8192 klsm16384 multiqC2 multiqC4 multiqC8 multiqC16
for DATASTRUCTURE in linden spraylist qdcatree fpaqdcatree fpaqdcatreenoputadapt fpaqdcatreenormminadapt fpaqdcatreenocatreeadapt klsm2048 klsm65536 globallock dlsm multiqC8 multiqC16
do
	python3.4 bench_file_shortest_path.py 1 $DATASTRUCTURE
done
