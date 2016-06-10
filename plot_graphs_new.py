#!/usr/bin/python
import re
import matplotlib
import sys
matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42
# font = {'family' : 'normal',
#         'weight' : 'normal',
#         'size'   : 22}
# matplotlib.rc('font', **font)

data_files=sys.argv[1:]

matplotlib.rcParams.update({'font.size': 6})



matplotlib.use('Agg')
import matplotlib.pyplot as plt
import sys

measurmentPoints = 1

def readFile(the_file, column=1):
    values = []
    with open(the_file, 'r') as f:
        lines = f.readlines()
        #lines = lines[2:]
        for l in lines:
            cols=l.split(" ")
            print(cols[column] + " column "  + str(column))
            values.append(float(cols[column]))
    return values

def plotCase(file_prefix, column, label, marker, ax, color, filter_points=[], number_of_nodes = 10):
    xvals = readFile(file_prefix, 0)
    x = []
    yvals = []
    y = []
    y_error_mins = []
    y_error_maxs = []
    for i in range(0, measurmentPoints):
        yvals.append(readFile(file_prefix , column))
    for line in range(0, len(xvals)):
        currentX = xvals[line]
        if currentX in filter_points:
            continue
        x.append(currentX)
        measurments = []
        for i in range(0, measurmentPoints):
            measurments.append(float(number_of_nodes) / yvals[i][line])
        average = sum(measurments)/measurmentPoints
        y.append(average)
        y_error_mins.append(average - min(measurments))
        y_error_maxs.append(max(measurments) -average)
    # xticks = [1,2,4,8,16,32,64,128]
    # xticklabels = ["1","2","4","8","16","32","64","128"]
    # ax.set_xscale('log', basey=2)
    # ax.set_xticks( xticks )
    # ax.set_xticklabels( xticklabels )
    # ax.get_xaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
    return ax.errorbar(x,y,[y_error_mins,y_error_maxs],label=label,linewidth=1, elinewidth=1,marker=marker,color=color)


def draw_graph(out_file,
               graph_title,
               legend = False,
               number_of_nodes = 10):
    plt.figure(figsize=(2.25,1.55))
    plt.xlabel('Number of Threads')
    plt.ylabel('Time')
    labels = []
    for (file_prefix, label, marker, color) in table_types_and_names:
        plotCase(file_prefix,
                 1, label, marker, plt.gca(), color, filter_points=[], number_of_nodes = number_of_nodes)
    if legend:
        plt.legend(bbox_to_anchor=(0., 1.02, 1., .102), loc=3,
                   ncol=1, mode="expand", borderaxespad=0.)
        #plt.legend(loc=0)
    #plt.tight_layout()
    #plt.legend(loc=legendLoc)
    plt.title(graph_title, fontsize=5.8,fontweight="bold")
    plt.savefig(out_file + '.pdf', bbox_inches='tight', pad_inches = 0)


#plot unweighted graphs
for weight_range in [0, 100, 1000, 10000, 1000000]:
    for (graph, number_of_nodes) in [("undirgrid", 1000000), ("roadnet", 1965206), ("live", 4847571), ("orkut", 3072442)]:
        table_types_and_names = [("./"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_1", "oldfpaqdcatree", '+', '#F68B00'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_fpaqdcatreenoputadapt_1", "oldfpaqdcatreenoputadapt", '+', 'b'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_fpaqdcatreenormminadapt_1", "oldfpaqdcatreenormminadapt", '+', 'g'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_fpaqdcatreenocatreeadapt_1", "oldfpaqdcatreenocatreeadapt", '+', '#EFF303'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_I", "fpaqdcatree", '*', '#F68B00'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_fpaqdcatreenoputadapt_I", "fpaqdcatreenoputadapt", '*', 'b'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_fpaqdcatreenormminadapt_I", "fpaqdcatreenormminadapt", '*', 'g'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_fpaqdcatreenocatreeadapt_I", "fpaqdcatreenocatreeadapt", '*', '#EFF303'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_multiqC2_1", "multiq2", 'o', '#373727'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_multiqC4_1", "multiq4", 'o', '#D137CC'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_multiqC8_1", "multiq8", 'o', '#3B0839'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_multiqC16_1", "multiq16", 'o', '#50A954'),                                ("./"+graph+"_"+ str(weight_range)+ "_linden_1", "linden", 'x', '#93875D'),
                                 #("./"+graph+"_"+ str(weight_range)+ "_globallock_1", "globallock", 's', '#93875D'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_klsm2048_1", "klsm2048", '+', '#93875D'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_klsm4096_1", "klsm4096", '+', '#3B0B17'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_klsm8192_1", "klsm8192", '+', '#8A0829'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_klsm16384_1", "klsm16384", '+', '#FF0040'),
                                 ("./"+graph+"_"+ str(weight_range)+ "_qdcatree_1", "qdcatree", '+', '#F5A9BC')# ,
                                 # ("smallq_res/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_smlq", "nerelsmlq", '+', '#3F9BBA'),
                                 # ("sandyputbuffres/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_buffput", "nerelsbuffput", '+', '#1EE335'),
                                 # ("fixputres/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_fixbput", "fixbput", 'x', '#95CC9B'),
                                 # ("better/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_agadopt", "better", 'x', '#E83807'),
                                 # ("fully_adapt_data/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_test_new_one_step_adopt", "one_step", 'x', '#8C47E6'),
                                 # ("fully_adapt_data/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_also_adapt_remove_min", "alsormmin", 'x', '#978AA8'),
                                 # ("fully_adapt_data/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_also_adapt_remove_min_only_one", "onlyone", 'x', '#6F00FF'),
                                 # ("fully_adapt_data2/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_also_adapt_remove_min_only_one_sml", "onlyonesml", 'x', '#FFAE00'),
                                 # ("hsml/"+graph+"_"+ str(weight_range)+ "_fpaqdcatree_also_adapt_remove_min_only_one_hsml", "onlyonehsml", '*', '#FFAE00')
        ]
        draw_graph(graph + " " + str(weight_range),
                   graph_title = graph + " " + str(weight_range),
                   legend = True,
                   number_of_nodes = number_of_nodes)

# draw_graph(file_prefix = "roadnet",
#            out_file = "roadnet_unweighted",
#            graph_title = "CA Road Network Unweighted",
#            legend = True)

# draw_graph(file_prefix = "undirgrid",
#            out_file = "undirgrid_unweighted",
#            graph_title = "Undirected Grid Unweighted",
#            legend = True)

# draw_graph(file_prefix = "livejournal",
#            out_file = "livejournal_unweighted",
#            graph_title = "LiveJournal Unweighted",
#            legend = True)

# draw_graph(file_prefix = "comorkut",
#            out_file = "comorkut_unweighted",
#            graph_title = "Orkut Unweighted",
#            legend = True)

# table_types_and_names = [(12, "CA-QD", '*', 'b'),
#                          (13, "batch del", 'o', 'r'),
#                          (14, "cached", 'o', 'g'),
#                          (16, "plain", '+', 'blue'),
#                          #(6, "CA-HQD", 'o', 'c'),
#                          (9, "SprayList", 's', '#FF5050'),
#                          (11, "LindenJonsson","^", 'y')]



# draw_graph(file_prefix = "roadnet",
#            out_file = "roadnet_weighted",
#            graph_title = "CA Road Network Weighted",
#            legend = True)

# draw_graph(file_prefix = "undirgrid",
#            out_file = "undirgrid_weighted",
#            graph_title = "Undirected Grid Weighted",
#            legend = True)

# draw_graph(file_prefix = "livejournal",
#            out_file = "livejournal_weighted",
#            graph_title = "LiveJournal Weighted",
#            legend = True)

# draw_graph(file_prefix = "comorkut",
#            out_file = "comorkut_weighted",
#            graph_title = "Orkut Weighted",
#            legend = True)
