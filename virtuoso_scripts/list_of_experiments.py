import os
import csv


def get_running_workloads():
    path = "./results/"

    experiments = []
    for experiment in os.listdir(path):
        if (os.path.isdir(path + experiment) == True):

            if (os.path.exists(path + experiment + "/sim.stats") == False):
                with open(path + experiment + "/sim.stdout", 'r') as out:
                    f = out.readlines()
                    if (f[-1].startswith("[STOPBYICOUNT]")):
                        time = f[-1].split(" ")[-1].strip("\n")
                        status = "R"

                    else:
                        time = "Uknown"
                        status = "U"
                experiments.append((experiment, status, "Running for "+time))
            else:
                with open(path + experiment + "/sim.stdout", 'r') as stats:
                    f = stats.readlines()
                    time = "Uknown"
                    for line in f:
                        if (line.startswith("[STOPBYICOUNT] Periodic")):
                            time = line.split(" ")[-1].strip("\n")

                    experiments.append((experiment, "C", "Completed in "+time))

    # Print header
    print("{:<37} {:<10} {}".format("Experiment", "Status", "Time"))

    # Assuming experiments is a list of tuples/lists
    for experiment in experiments:
        print("{:<37} {:<10} {}".format(
            experiment[0], experiment[1], experiment[2]))

    # print how many experiments are running and how many are completed

    running = 0
    completed = 0

    for experiment in experiments:
        if (experiment[1] == "R"):
            running += 1
        elif (experiment[1] == "C"):
            completed += 1

    print("Running: "+str(running)+" out of " +
          str(len(experiments))+" experiments")

    print("Completed: "+str(completed)+" out of " +
          str(len(experiments))+" experiments")
