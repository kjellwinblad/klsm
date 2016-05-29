import sys
import subprocess
THREAD_COUNTS=[1,2,4,8,16,32,63,64]
EDGE_WEIGHT_RANGES=[0,100,10000,1000000]
GRAPH_FILES=[("com-orkut.ungraph.txt", "orkut"), ("grid1000_undir.txt", "undirgrid"), ("roadNet-CA.txt", "roadnet"),  ("soc-LiveJournal1.txt","live")]
post_fix = sys.argv[1]
data_structure = sys.argv[2]

for (graph_file, graph_file_name) in GRAPH_FILES:
    for edge_weight in EDGE_WEIGHT_RANGES:
        edge_weight_argument = []
        if edge_weight != 0:
            edge_weight_argument = ["-w", str(edge_weight)]        
        command = ["build/src/bench/file_shortest_paths", "-n", "1", "-i", graph_file,  "-o", "model_output"] + edge_weight_argument + ["globallock"]
        print("producing model with command command: " + " ".join(command))
        output = subprocess.check_output(command).decode("utf-8") 
        print("model producing output: " + str(output))
        output_file_name = graph_file_name +"_"+ str(edge_weight) + "_" + data_structure + "_" + post_fix
        result_output = open(output_file_name, 'w')
        for thread_count in THREAD_COUNTS:
            command = ["build/src/bench/file_shortest_paths", "-n", str(thread_count), "-i", graph_file,  "-o", "output_to_check"] + edge_weight_argument + [data_structure]
            print("running command: " + " ".join(command))
            output = subprocess.check_output(command).decode("utf-8") 
            print("output: " + str(output))
            diff_output = subprocess.check_output(["diff", "model_output", "output_to_check"]).decode("utf-8")
            if(diff_output != ""):
                print("DIFF FROM MODEL")
                result_output.write(str(thread_count) + " error!")
                sys.exit(1)
            result_output.write(str(thread_count) + " " + str(output))
        result_output.close()
