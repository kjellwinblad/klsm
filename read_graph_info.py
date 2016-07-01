import sys


import networkx as nx
G=nx.DiGraph()

f = open(sys.argv[1], 'r')


first_line = f.readline().split(" ")## Nodes: 1965206 Edges: 5533214
nr_nodes = int(first_line[2])
nr_edges = int(first_line[4])
print("HEADER: " + str(nr_nodes) + " nodes " + str(nr_edges) + " edges" )
for i in range(0, nr_edges):
    line = f.readline().split("\t")
    source = int(line[0])
    target = int(line[1])
    if source >= nr_nodes or target >= nr_nodes:
        continue
    G.add_edge(source,target)
print("READ EDGES")

print("Tot nodes " + str(len(G.nodes())) + " Tot edges " + str(len(G.edges())))

nodes = nx.shortest_path(G, 0).keys()

print("Number of nodes connected to 0 " + str(len(nodes)) )

S = G.subgraph(nodes)

print("Number of edges among nodes reachable from 0 " + str(len(S.edges())) )
