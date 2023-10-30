
import os
import csv
from list_of_experiments import get_running_workloads

stats = {
"Trace": None,
"Exp": None,
"mmu.total_table_walk_latency": None,
"mmu.page_faults":None,
"mmu.tlb_latency_0": None,
"mmu.tlb_latency_1":None,
"mmu.total_table_walk_latency":None,
"mmu.total_tlb_latency":None,
"mmu.total_translation_latency":  None,
"range_lb.accesses": None,
"range_lb.hits": None,
"range_lb.misses": None
}

# Create a CSV with all these headers
with open('results.csv', 'w') as csvfile:

    # Write the headers
    writer = csv.DictWriter(csvfile, fieldnames=stats.keys())
    writer.writeheader()

    path = "./results/"

    for experiment in os.listdir(path):
        if (os.path.isdir(path + experiment) == True):
            row = {}
            config, trace = experiment.rsplit('_', 1)
            print (config, trace)
            row["Trace"] = trace
            row["Exp"] = config
            # Check if the sim.stats file exists
            if (os.path.exists(path+experiment+"/sim.stats") == False):
                with open(path+experiment+"/sim.stdout") as f:
                    lines = f.readlines()
                    if (lines[-1].startswith("[STOPBYICOUNT]")):
                        time = lines[-1].split(" ")[-1]
                    else:
                        time = "Uknown"
                    print("The sim.stats file does not exist, since the job: " +
                          experiment + " did not finish: Running for "+time)
                    get_running_workloads()
                    print("Cannot create the complete CSV file, exiting...")
                    exit(6)
            with open(path + experiment + "/sim.stats") as f:

                lines = f.readlines()
                for line in lines:
                    key = line.split("=")[0]
                    value = line.split("=")[1]
                    key = key.replace(" ", "")

                    if key in stats:
                        row[key] = float(value)
            print (row)
            writer.writerow(row)
