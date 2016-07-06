import sys
import subprocess
THREAD_COUNTS=[1,2,4,8,16,24,32,48,64]
EDGE_WEIGHT_RANGES=[0,1000,1000000]
GRAPH_FILES=[("roadNet-CA.txt", "roadnet"),("soc-LiveJournal1.txt","live"), ("randn1000000p0.0001.txt", "randn1000000p0.0001"), ("randn10000p0.5.txt", "randn10000p0.5")]
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
            output = "9999999"
            was_error = False
            try:
                output = subprocess.check_output(command).decode("utf-8") 
                print("output: " + str(output))
                diff_output = subprocess.check_output(["diff", "model_output", "output_to_check"], timeout=600).decode("utf-8")
                if(diff_output != ""):
                    print("DIFF NO CRASH")
                    was_error = True
            except:
                was_error = True
            if was_error:
                print("DIFF FROM MODEL")
                result_output.write(str(thread_count) + " 9999999")
            else:
                result_output.write(str(thread_count) + " " + str(output))
        result_output.close()
