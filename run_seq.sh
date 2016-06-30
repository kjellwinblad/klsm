#!/bin/sh

# echo roadnet unweighted
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -o seq fibheap

# echo roadnet weight 1000
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -w 1000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -w 1000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -w 1000 -o seq fibheap

# echo roadnet weight 1000000
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -w 1000000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -w 1000000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i roadNet-CA.txt -w 1000000 -o seq fibheap


# echo live unweighted
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -o seq fibheap

# echo live weight 1000
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -w 1000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -w 1000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -w 1000 -o seq fibheap

# echo live weight 1000000
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -w 1000000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -w 1000000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i soc-LiveJournal1.txt -w 1000000 -o seq fibheap


echo rand n 10000 p 0.5 unweighted
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -o seq fibheap
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -o seq fibheap
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -o seq fibheap

echo rand n 10000 p 0.5 weigh 1000
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -w 1000 -o seq fibheap
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -w 1000 -o seq fibheap
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -w 1000 -o seq fibheap

echo rand n 10000 p 0.5 weight 1000000
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -w 1000000 -o seq fibheap
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -w 1000000 -o seq fibheap
build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i random_graph.txt -w 1000000 -o seq fibheap


# echo rand n 1000000 p 0.0001 unweighted
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -o seq fibheap

# echo rand n 1000000 p 0.0001 weigh 1000
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -w 1000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -w 1000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -w 1000 -o seq fibheap

# echo rand n 1000000 p 0.0001 weight 1000000
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -w 1000000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -w 1000000 -o seq fibheap
# build/src/bench/file_shortest_paths_seq_no_lazy_relax -n 1 -i rand_graph3.txt -w 1000000 -o seq fibheap
