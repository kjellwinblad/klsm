/*
 *  Copyright 2016 Kjell Winblad
 *  Copyright 2014 Jakob Gruber
 *
 *  This file is part of kpqueue.
 *
 *  kpqueue is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  kpqueue is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with kpqueue.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctime>
#include <future>
#include <getopt.h>
#include <random>
#include <thread>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstdlib>

#include "pqs/globallock.h"
#include "pqs/multiq.h"
#include "dist_lsm/dist_lsm.h"
#include "k_lsm/k_lsm.h"
#include "util.h"


constexpr int DEFAULT_NTHREADS   = 1;
constexpr int DEFAULT_RELAXATION = 256;
constexpr int DEFAULT_SEED       = 0;
constexpr size_t DEFAULT_RANDOM_WEIGHTS_END_RANGE = 100;
static std::string DEFAULT_OUTPUT_FILE = "out.txt";

#define PQ_DLSM       "dlsm"
#define PQ_GLOBALLOCK "globallock"
#define PQ_KLSM       "klsm"
#define PQ_MULTIQ     "multiq"

static hwloc_wrapper hwloc; /**< Thread pinning functionality. */

static std::atomic<bool> start_barrier(false);

struct threads_waiting_to_succeed_pad {
    char pad1[128];
    std::atomic<int> threads_waiting_to_succeed;
    char pad2[128];
};

static threads_waiting_to_succeed_pad wt;

struct settings {
    int num_threads;
    std::string graph_file;
    int seed;    
    size_t max_generated_random_weight;
    std::string output_file;
    std::string type;
};

struct edge_t {
    size_t target;
    size_t weight;
};

struct vertex_t {
    size_t num_edges;
    std::atomic<size_t> distance;
    edge_t *edges;
};

static void
usage()
{
    fprintf(stderr,
            "USAGE: shortest_paths -i input_file [-n num_threads] [-w end_of_range]\n"
            "                      [-s seed] pq\n"
            "       -i: The input graph file\n"
            "       -n: Number of threads (default = %d)\n"
            "       -w: Generate random weights between 0 and end_of_range (default = %lu)\n"
            "       -s: The random number generator seed (default = %d)\n"
            "       -o: Output file name (default = %s)\n"
            "       pq: The data structure to use as the backing priority queue\n"
            "           (one of '%s', %s', '%s', '%s')\n",
            DEFAULT_NTHREADS,
            DEFAULT_RANDOM_WEIGHTS_END_RANGE,
            DEFAULT_SEED,
            DEFAULT_OUTPUT_FILE.c_str(),
            PQ_DLSM, PQ_GLOBALLOCK, PQ_KLSM, PQ_MULTIQ);
    exit(EXIT_FAILURE);
}

static vertex_t *
read_graph(std::string file_path,
           bool generate_weights,
           size_t generate_weights_range_end,
           size_t & n,
           int seed)
{
    std::ifstream file;
    file.open(file_path);
    if ( ! file.is_open()) {
        std::cout << "Could not open file: " << file_path << std::endl;
        std::exit(0);
    }
    size_t tmp_num1;
    size_t tmp_num2;
    std::string tmp_str;
    file >> tmp_str >> tmp_str >> tmp_num1 >> tmp_str >>  tmp_num2;    
    if(tmp_num1 == 3072442){/*Hack for orkut graph that seems to be badly formatted*/
        n = tmp_num1 + 200;
    }else{
        n = tmp_num1;
    }
    vertex_t *data = new vertex_t[n];
    size_t *current_edge_index = new size_t[n];
    for(size_t i = 0; i < n; i++){
        data[i].num_edges = 0;
        data[i].distance.store(std::numeric_limits<size_t>::max(), std::memory_order_relaxed );
        data[i].edges = NULL;
    }
    for(size_t i = 0; i < n; i++){
        current_edge_index[i] = 0;
    }
    int line = 0;
    while (!file.eof())
    {
        file >> tmp_num1 >> tmp_num2;
        line++;
        data[tmp_num1].num_edges++;
    }
    file.close();
    file.open(file_path);
    if ( ! file.is_open()) {
        std::cout << "Could not open file: " << file_path << std::endl;
        std::exit(0);
    }
    file >> tmp_str >> tmp_str >> tmp_num1 >> tmp_str >>  tmp_num2;
    std::mt19937 rng;
    rng.seed(seed);
    std::uniform_int_distribution<size_t> rnd_st(0, generate_weights_range_end);
    while (!file.eof())
    {
        file >> tmp_num1 >> tmp_num2;
        if(data[tmp_num1].edges == NULL){
            data[tmp_num1].edges = new edge_t[data[tmp_num1].num_edges];
        }
        data[tmp_num1].edges[current_edge_index[tmp_num1]].target = tmp_num2;
        if(generate_weights){
            data[tmp_num1].edges[current_edge_index[tmp_num1]].weight = 1;
        }else{
            data[tmp_num1].edges[current_edge_index[tmp_num1]].weight = rnd_st(rng);;
        }
        current_edge_index[tmp_num1]++;
    }
    delete[] current_edge_index;
    return data;
}

static void
verify_graph(const vertex_t *graph,
             const size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const vertex_t *v = &graph[i];
        const size_t v_dist = v->distance.load(std::memory_order_relaxed);

        for (size_t j = 0; j < v->num_edges; j++) {
            const edge_t *e = &v->edges[j];
            const size_t new_dist = v_dist + e->weight;

            const vertex_t *w = &graph[e->target];
            const size_t w_dist = w->distance.load(std::memory_order_relaxed);

            assert(new_dist >= w_dist), (void)new_dist, (void)w_dist;
        }
    }
}

static void
print_graph(const vertex_t *graph,
            const size_t n,
            std::string out_file)
{
    std::ofstream file(out_file);
    if (!file.is_open()){
        std::cout << "Unable to open out file: " << out_file << std::endl;
        std::exit(0);
    }
    for (size_t i = 0; i < n; i++) {
        const vertex_t *v = &graph[i];
        const size_t v_dist = v->distance.load(std::memory_order_relaxed);
        if(v_dist == std::numeric_limits<size_t>::max()){
            file << i << " -1\n";
        }else{
            file << i << " " << v_dist << "\n";
        }
    }
    file.close();
}

static void
delete_graph(vertex_t *data,
             const size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (data[i].edges != NULL) {
            delete[] data[i].edges;
        }
    }
    delete[] data;
}

template <class T>
static void
bench_thread(T *pq,
             const int number_of_threads,
             const int thread_id,
             vertex_t *graph)
{
    (void)number_of_threads;
    hwloc.pin_to_core(thread_id);
    while (!start_barrier.load(std::memory_order_relaxed)) {
        /* Wait. */
    }
    //bool last_success = true;
    //int threads_waiting;
    //(threads_waiting = wt.threads_waiting_to_succeed.load(std::memory_order_relaxed)) < number_of_threads
    std::cout << "Start\n";
    while (true) {
        // if(!last_success && threads_waiting == 0){
        //     last_success = true;
        // }else if(!last_success){
        //     std::this_thread::yield();
        //     continue;
        // }
        size_t distance;
        size_t node;
        if (!pq->delete_min(distance, node)) {
            bool success = false;
            for(int i = 0 ; i < 100; i++){
                if (pq->delete_min(distance, node)){
                    success = true;
                    break;
                }
                std::this_thread::yield();
            }
            if(!success){
                std::cout << "exit\n";
                //We give up... No work for us
                break;
            }
            //last_success = false;
            //wt.threads_waiting_to_succeed.fetch_add(1);
            
        }
        //std::cout << "NODE DELTED: " << node << " DISTANCE " << distance << std::endl << std::flush;
        // if(threads_waiting > 0){
        //     wt.threads_waiting_to_succeed = 0;
        // }
        const vertex_t *v = &graph[node];
        const size_t v_dist = v->distance.load(std::memory_order_relaxed);

        if (distance > v_dist) {
            /* Better estimate already found */
            continue;
        }
        for (size_t i = 0; i < v->num_edges; i++) {
            const edge_t *e = &v->edges[i];
            const size_t new_dist = v_dist + e->weight;

            vertex_t *w = &graph[e->target];
            size_t w_dist = w->distance.load(std::memory_order_relaxed);

            if (new_dist >= w_dist) {
                continue;
            }

            bool dist_updated;
            do {
                dist_updated = w->distance.compare_exchange_strong(w_dist, new_dist,
                               std::memory_order_relaxed);
            } while (!dist_updated && w_dist > new_dist);

            if (dist_updated) {
                pq->insert(new_dist, e->target);
            }
        }
    }
}

template <class T>
static int
bench(T *pq,
      const struct settings &settings)
{
    if (settings.num_threads > 1 && !pq->supports_concurrency()) {
        fprintf(stderr, "The given data structure does not support concurrency.\n");
        return -1;
    }

    int ret = 0;
    size_t number_of_nodes;
    vertex_t *graph = read_graph(settings.graph_file,
                                 settings.max_generated_random_weight != 0,
                                 settings.max_generated_random_weight,
                                 number_of_nodes,
                                 settings.seed);


    /* Our initial node is graph[0]. */

    pq->insert(0, 0);
    graph[0].distance.store(0);

    /* Start all threads. */

    std::vector<std::thread> threads(settings.num_threads);
    wt.threads_waiting_to_succeed = 0;
    for (int i = 0; i < settings.num_threads; i++) {
        threads[i] = std::thread(bench_thread<T>, pq, settings.num_threads, i, graph);
    }

    /* Begin benchmark. */
    start_barrier.store(true, std::memory_order_relaxed);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (auto &thread : threads) {
        thread.join();
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    /* End benchmark. */

    verify_graph(graph, number_of_nodes);
    print_graph(graph,
                number_of_nodes,
                settings.output_file);
    
    const double elapsed = timediff_in_s(start, end);
    fprintf(stdout, "%f\n", elapsed);

    delete_graph(graph, number_of_nodes);
    return ret;
}

int
main(int argc,
     char **argv)
{
    int ret = 0;
    struct settings s = { DEFAULT_NTHREADS, "", DEFAULT_SEED, DEFAULT_RANDOM_WEIGHTS_END_RANGE, DEFAULT_OUTPUT_FILE, PQ_DLSM};

    int opt;
    while ((opt = getopt(argc, argv, "i:n:w:o:p:s:")) != -1) {
        switch (opt) {
        case 'i':
            errno = 0;
            s.graph_file = optarg;
            if (errno != 0) {
                usage();
            }
            break;
        case 'n':
            errno = 0;
            s.num_threads = strtol(optarg, NULL, 0);
            if (errno != 0) {
                usage();
            }
            break;
        case 'w':
            errno = 0;
            s.max_generated_random_weight = strtod(optarg, NULL);
            if (errno != 0) {
                usage();
            }
            break;
        case 's':
            errno = 0;
            s.seed = strtol(optarg, NULL, 0);
            if (errno != 0) {
                usage();
            }
            break;
        case 'o':
            errno = 0;
            s.output_file = optarg;
            if (errno != 0) {
                usage();
            }
            break;
        default:
            usage();
        }
    }

    if (s.graph_file == "") {
        usage();
    }

    if (s.num_threads < 1) {
        usage();
    }


    if (optind != argc - 1) {
        usage();
    }

    s.type = argv[optind];

    if (s.type == PQ_DLSM) {
        kpq::dist_lsm<size_t, size_t, DEFAULT_RELAXATION> pq;
        ret = bench(&pq, s);
    }  else if (s.type == PQ_KLSM) {
         kpq::k_lsm<size_t, size_t, DEFAULT_RELAXATION> pq;
         ret = bench(&pq, s);
    }else if (s.type == PQ_GLOBALLOCK) {
         kpqbench::GlobalLock<size_t, size_t> pq;
         ret = bench(&pq, s);
    }// else if (s.type == PQ_GLOBALLOCK) {
    //     kpqbench::GlobalLock<size_t, size_t> pq;
    //     ret = bench(&pq, s);
    // } else if (s.type == PQ_MULTIQ) {
    //     kpqbench::multiq<size_t, size_t> pq(s.num_threads);
    //     ret = bench(&pq, s);
    // }
    else {
        usage();
    }

    return ret;
}
