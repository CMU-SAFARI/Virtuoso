import subprocess
import os

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
from mpl_toolkits.axes_grid1 import host_subplot
import matplotlib.ticker as ticker
import mpl_toolkits.axisartist as AA




plt.style.use("grayscale")
plt.rcParams['font.family'] = 'serif'
plt.rcParams['font.serif'] = 'Ubuntu'
plt.rcParams['font.monospace'] = 'Ubuntu Mono'
#plt.rcParams['font.size'] = 10
plt.rcParams['font.weight']='bold'
plt.rcParams['axes.labelsize'] = 9
plt.rcParams['axes.labelweight'] =  'bold'
plt.rcParams['axes.titlesize'] = 10
plt.rcParams['xtick.labelsize'] = 5
plt.rcParams['ytick.labelsize'] = 7
plt.rcParams['legend.fontsize'] = 5
bar_width=0.2

fig = matplotlib.pyplot.gcf()
fig.set_size_inches(4.5,1.5)

colors = ["#C0C0C0","#b3cde0","#6497b1","#005b96","#808080","#000000"]




sniper_exe  = "~/megapages/sniper/run-sniper"


configs = []
configs.append(" -c ~/megapages/sniper/config/megapage_tlb150.cfg")
configs.append(" -c ~/megapages/sniper/config/megapage_tlb200.cfg")
configs.append(" -c ~/megapages/sniper/config/megapage_tlb250.cfg")
configs.append(" -c ~/megapages/sniper/config/megapage_tlb300.cfg")



app_arg = " -- "
apps = []

apps.append("/mnt/panzer/kstojiljk/megapages/graph_workloads/benchmarks/RADII/radii.e /mnt/local/kstojiljk/megapages/graph_workloads/inputs/rMat33mxq")

stats_n = 7
stats = [[[0.0 for y in range(len(apps))] for x in range(len(configs))] for y in range(stats_n)]


args = " -s stop-by-icount:10000000 --roi --no-cache-warming "

i = 0 
j = 0 
last = ""
for app in apps:
    for config in configs:
        output = "/mnt/panzer/kstojiljk/megapages_results/outputs/"+app.split(" ")[0].split("/")[-1].split(".")[0] + "_" + app.split(" ")[1].split("/")[-1].split(".")[0] +"_"+ config.split(" ")[2].split("/")[-1]
        print(sniper_exe+args+"-d "+output+config+app_arg+app)
        print(app.split(" ")[0].split("/")[-1].split(".")[0])
        os.system("srun -w kratos9  -e ./err_"+app.split(" ")[0].split("/")[-1].split(".")[0] + "_" + app.split(" ")[1].split("/")[-1].split(".")[0] +"_"+ config.split(" ")[2].split("/")[-1]+" -o ./out_"+app.split(" ")[0].split("/")[-1].split(".")[0] + "_" + app.split(" ")[1].split("/")[-1].split(".")[0] +"_"+ config.split(" ")[2].split("/")[-1]+" "+sniper_exe+args+" -d "+output+config+app_arg+app + " &")

        j = j + 1
    i = i + 1 
    j=0

