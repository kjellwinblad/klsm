/*
 *  Copyright 2017 Kjell Winblad and Jakob Gruber
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

#include <cassert>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <future>
#include <getopt.h>
#include <iostream>
#include <random>
#include <thread>
#include <unistd.h>

#include "fiboheap.h"
#include "util.h"

constexpr int DEFAULT_NTHREADS   = 1;
constexpr int DEFAULT_SEED       = 0;
static std::string DEFAULT_OUTPUT_FILE = "out.txt";


/* Hack to support graphs that are badly formatted */
#define IGNORE_NODES_WITH_ID_LARGER_THAN_SIZE 1

//#define PAPI 1

#ifdef PAPI
extern "C" {
#include <papi.h>
}
#define G_EVENT_COUNT 2
#define MAX_THREADS 80
int g_events[] = { PAPI_L1_DCM, PAPI_L2_DCM };
long long g_values[MAX_THREADS][G_EVENT_COUNT] = {0,};
#endif

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
    size_t distance;
    FibHeap<size_t, size_t>::FibNode *n;
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
            "       -w: Generate random weights between 0 and end_of_range\n"
            "       -s: The random number generator seed (default = %d)\n"
            "       -o: Output file name (default = %s)\n"
            "       pq: The data structure to use as the backing priority queue\n"
            "           (one of '%s')\n",
            DEFAULT_NTHREADS,
            DEFAULT_SEED,
            DEFAULT_OUTPUT_FILE.c_str(),
            "fibheap");
    exit(EXIT_FAILURE);
}

static vertex_t *
read_graph(std::string file_path,
           bool generate_weights,
           size_t generate_weights_range_end,
           size_t &number_of_nodes_write_back,
           int seed)
{
    std::ifstream file;
    file.open(file_path);
    if (! file.is_open()) {
        std::cerr << "Could not open file: " << file_path << std::endl;
        std::exit(0);
    }
    size_t tmp_num1;
    size_t tmp_num2;
    std::string tmp_str;
    file >> tmp_str >> tmp_str >> tmp_num1 >> tmp_str >>  tmp_num2;
    number_of_nodes_write_back = tmp_num1;
    size_t n = tmp_num1;
    vertex_t *data = new vertex_t[n];
    size_t *current_edge_index = new size_t[n];
    for (size_t i = 0; i < n; i++) {
        data[i].num_edges = 0;
        data[i].distance = std::numeric_limits<size_t>::max();
        data[i].n = NULL;
        //data[i].processed = false;
        data[i].edges = NULL;
    }
    for (size_t i = 0; i < n; i++) {
        current_edge_index[i] = 0;
    }
    while (!file.eof()) {
        file >> tmp_num1 >> tmp_num2;
#ifdef IGNORE_NODES_WITH_ID_LARGER_THAN_SIZE
        if (tmp_num1 >= n) {
            continue;
        }
        if (tmp_num2 >= n) {
            continue;
        }
#endif
        data[tmp_num1].num_edges++;
    }
    file.close();
    file.open(file_path);
    if (! file.is_open()) {
        std::cerr << "Could not open file: " << file_path << std::endl;
        std::exit(0);
    }
    file >> tmp_str >> tmp_str >> tmp_num1 >> tmp_str >>  tmp_num2;
    std::mt19937 rng;
    rng.seed(seed);
    std::uniform_int_distribution<size_t> rnd_st(0, generate_weights_range_end);
    while (!file.eof()) {
        file >> tmp_num1 >> tmp_num2;
#ifdef IGNORE_NODES_WITH_ID_LARGER_THAN_SIZE
        if (tmp_num1 >= n) {
            continue;
        }
        if (tmp_num2 >= n) {
            continue;
        }
#endif
        if (data[tmp_num1].edges == NULL) {
            data[tmp_num1].edges = new edge_t[data[tmp_num1].num_edges];
        }
        data[tmp_num1].edges[current_edge_index[tmp_num1]].target = tmp_num2;
        if (!generate_weights) {
            data[tmp_num1].edges[current_edge_index[tmp_num1]].weight = 1;
        } else {
            data[tmp_num1].edges[current_edge_index[tmp_num1]].weight = rnd_st(rng);;
        }
        current_edge_index[tmp_num1]++;
    }
    delete[] current_edge_index;
    return data;
}

static void
print_graph(const vertex_t *graph,
            const size_t n,
            std::string out_file)
{
    std::ofstream file(out_file);
    if (!file.is_open()) {
        std::cerr << "Unable to open out file: " << out_file << std::endl;
        std::exit(0);
    }
    for (size_t i = 0; i < n; i++) {
        const vertex_t *v = &graph[i];
        const size_t v_dist = v->distance;
        if (v_dist == std::numeric_limits<size_t>::max()) {
            file << i << " -1\n";
        } else {
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

static void start_sssp(FibHeap<size_t, size_t> *pq,
                       vertex_t *graph)
{

#ifdef PAPI
    if (PAPI_OK != PAPI_start_counters(g_events, G_EVENT_COUNT)) {
        std::cout << ("Problem starting counters 1.\n");
    }
#endif


    while (!pq->empty()) {
        size_t distance;
        size_t node;
        pq->pop(distance, node);
        vertex_t *v = &graph[node];
        size_t v_dist = v->distance;
        for (size_t i = 0; i < v->num_edges; i++) {
            const edge_t *e = &v->edges[i];
            const size_t new_dist = v_dist + e->weight;
            vertex_t *w = &graph[e->target];
            size_t w_dist = w->distance;

            if (new_dist < w_dist) {
                w->distance = new_dist;
                if (w->n == NULL) {
                    w->n = pq->push(new_dist, e->target);
                } else {
                    pq->decrease_key(w->n, new_dist);
                }
            }
        }
    }
#ifdef PAPI
    if (PAPI_OK != PAPI_read_counters(g_values[0], G_EVENT_COUNT)) {
        std::cout << ("Problem reading counters 2.\n");
    }
#endif
}

static int
bench(FibHeap<size_t, size_t> *pq,
      const struct settings &settings)
{

    int ret = 0;
    size_t number_of_nodes;
    vertex_t *graph = read_graph(settings.graph_file,
                                 settings.max_generated_random_weight != 0,
                                 settings.max_generated_random_weight,
                                 number_of_nodes,
                                 settings.seed);


    /* Our initial node is graph[0]. */

    graph[0].n = pq->push((size_t)0, (size_t)0);

    graph[0].distance = 0;


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    start_sssp(pq, graph);

    clock_gettime(CLOCK_MONOTONIC, &end);
    /* End benchmark. */

    //verify_graph(graph, number_of_nodes);
    print_graph(graph,
                number_of_nodes,
                settings.output_file);

    const double elapsed = timediff_in_s(start, end);
    fprintf(stdout, "%f", elapsed);

    delete_graph(graph, number_of_nodes);
    return ret;
}

int
main(int argc,
     char **argv)
{
    int ret = 0;
    struct settings s = { DEFAULT_NTHREADS, "", DEFAULT_SEED, 0, DEFAULT_OUTPUT_FILE, "fibheap"};

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
            s.max_generated_random_weight = strtol(optarg, NULL, 0);
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


#ifdef PAPI
    if (PAPI_VER_CURRENT != PAPI_library_init(PAPI_VER_CURRENT)) {
        std::cout << ("PAPI_library_init error.\n");
        return 0;
    }

    if (PAPI_OK != PAPI_query_event(PAPI_L1_DCM)) {
        std::cout << ("Cannot count PAPI_L1_DCM.\n");
    }
    if (PAPI_OK != PAPI_query_event(PAPI_L2_DCM)) {
        std::cout << ("Cannot count PAPI_L2_DCM.");
    }
#endif


    if (s.type == "fibheap") {
        FibHeap<size_t, size_t> pq;
        ret = bench(&pq, s);
    } else {
        usage();
        return ret;
    }
#ifdef PAPI
    long total_L2_cache_accesses = 0;
    long total_L2_cache_misses = 0;
    int k = 0;
    for (k = 0; k <  s.num_threads; k++) {
        total_L2_cache_accesses += g_values[k][0];
        total_L2_cache_misses += g_values[k][1];
    }
    printf(" %ld %ld", total_L2_cache_accesses, total_L2_cache_misses);
#endif
    printf("\n");
    return ret;
}
