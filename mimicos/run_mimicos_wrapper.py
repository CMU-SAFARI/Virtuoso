import sys
import os 

# Write a python function that executes this command:  ./build/startup_virtuos  and takes three parameters as arguments


def run_virtuos_wrapper(virtuos_config, app_trace, virtuos_output):

    # Check if the file exists
    if not os.path.exists(virtuos_config):
        print(f"File {virtuos_config} does not exist")
        return

    if not os.path.exists(app_trace):
        print(f"File {app_trace} does not exist")
        return

    # Check if the file is a file
    if not os.path.isfile(virtuos_config):
        print(f"{virtuos_config} is not a file")
        return

    if not os.path.isfile(app_trace):
        print(f"{app_trace} is not a file")
        return

    # Check if the file is empty
    if os.stat(virtuos_config).st_size == 0:
        print(f"{virtuos_config} is empty")
        return

    if os.stat(app_trace).st_size == 0:
        print(f"{app_trace} is empty")
        return

    # Check if the file is a directory
    if os.path.isdir(virtuos_config):
        print(f"{virtuos_config} is a directory")
        return

    if os.path.isdir(app_trace):
        print(f"{app_trace} is a directory")
        return

    # Check if the file is readable
    if not os.access(virtuos_config, os.R_OK):
        print(f"{virtuos_config} is not readable")
        return

    if not os.access(app_trace, os.R_OK):
        print(f"{app_trace} is not readable")
        return

    # Check if the file is writable
    if not os.access(virtuos_output, os.W_OK):
        print(f"{virtuos_output} is not writable")
        return

    # Execute the command
    command = f"./build/startup_virtuos {virtuos_config} {app_trace} {virtuos_output}"
    os.system(command)


if __name__ == "__main__":

    if len(sys.argv) != 4:
        print("Usage: python run_virtuos_wrapper.py <virtuos_config> <app_trace> <virtuos_output>")
        print("Setting default values")
        virtuos_config = "./configs/reservethp_32GB.ini"
        app_trace = os.environ.get("VIRTUOSO_ROOT", ".") + "/simulator/sniper/traces_victima/rnd.sift"
        virtuos_output = "./test_output/output"

    else: 
        virtuos_config = sys.argv[1]
        app_trace = sys.argv[2]
        virtuos_output = sys.argv[3]

    run_virtuos_wrapper(virtuos_config, app_trace, virtuos_output)

