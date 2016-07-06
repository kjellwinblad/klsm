This repository is a fork of the kpqueue repository by Jakob Gruber
(https://github.com/schuay/kpqueue). I plan to merge relevant code
into the kpqueue repository. Contact Kjell Winblad
(kjell.winblad@it.uu.se) if you have questions about this.

The fork was created to experiment with the contention adapting
priority queue (CA-PQ) (http://www.it.uu.se/research/group/languages/software/ca_pq).

CA-PQ Implementation and SSSP benchmark
=======================================

The main file for the implementation of CA-PQ is `lib/qdcatree/skiplist_fpaqdcatree_set.c`.

The main file for the SSSP benchmark that is used in the CA-PQ paper is `src/bench/file_shortest_paths.cpp`.

The sequential Dijkstra's shortest path algorithm is implemented in `src/bench/file_shortest_paths_seq_no_lazy_relax.cpp`.

The program that generate random graphs is implemented in `src/bench/generate_random_graph.cpp`.

Compiling
=========

`make`

To compile the build tool cmake is required. It should be possible to
figure out from the error messages one get when running `make` which
other dependencies are needed.

Running benchmarks
==================

The RoadNet graph can be downloaded from:

`https://snap.stanford.edu/data/roadNet-CA.html`

The LiveJournal graph can be downloaded from:

`https://snap.stanford.edu/data/soc-LiveJournal1.html`

To generate the Erd\H{o}s-R\'enyi graph with N=1000000 and P=0.0001 run the following command (takes a long time):

`build/src/bench/generate_random_graph -n 1000000 -p 0.0001 > randn1000000p0.0001.txt`

To generate the Erd\H{o}s-R\'enyi graph with N=10000 and P=0.5 run the following command:

`build/src/bench/generate_random_graph -n 10000 -p 0.5 > randn10000p0.5.txt`

The benchmarks can then be ran with the script `run_all.sh`. The file
`bench_file_shortest_path.py` can be modified to select which thread
counts and scenarios that should be used. Which data structures that
should be included in the benchmark can be changed by modifying
`run_all.sh`. The name for CA-PQ is fpaqdcatree, the name for CA-IN is
fpaqdcatreenormminadapt and the mane for CA-DM is
fpaqdcatreenoputadapt. The output of the benchmark is placed in files
named with the pattern:

`graphname_weights_datastructure_1`


For example the results from the LiveJournal graph with edge weights
from the range in [0,1000] for CA-PQ are placed in the file
`live_1000_fpaqdcatree_1`. The files with the weights part of the file
name set to 0 contain the results for the graphs with a weight of 1 on
all edges. The first column in the result files show the thread count,
the second column shows the execution time and the third column shows
the waste measurement.

NOTE THAT: The automatic pinning with hwloc might not work correctly
on all platforms which can cause weird looking results so one might
need to uncomment the define `MANUAL_PINNING` in
`src/bench/file_shortest_paths.cpp` and modify the pinning code to fit
the machine at hand.