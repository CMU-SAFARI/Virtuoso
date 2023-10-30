#!/usr/bin/env python

# BEGIN_LEGAL
# BSD License
#
# Copyright (c)2015 Intel Corporation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.  Redistributions
# in binary form must reproduce the above copyright notice, this list of
# conditions and the following disclaimer in the documentation and/or
# other materials provided with the distribution.  Neither the name of
# the Intel Corporation nor the names of its contributors may be used to
# endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
# ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# END_LEGAL
#
#
# @ORIGINAL_AUTHORS: T. Mack Stallcup, Cristiano Pereira, Harish Patil, Chuck Yount
#
#
# $Id: config.py,v 1.122 2016/03/30 14:52:42 tmstall Exp tmstall $

import ConfigParser
import optparse
import os
import pickle
import platform
import random
import shutil
import subprocess
import tempfile
import types

# Local modules
#
import msg

#####################################################################################
#
# The 'config' module is imported by all tracing modules.  
#
# Global data attributes which is used by every module is stored here.  The
# class GlobalVar reads/writes pickle files, i.e. 'global data' files which are
# used to transfer the global data from calling Python scripts to the called
# Python scripts.
#
# The class ConfigClass reads/writes tracing parameter configuration files.
#
#####################################################################################

"""
Here's the process for adding new parameters.

1) Since all parameters can be also initilized from command options, the first
   step is to add a new option to the module 'cmd_options.py'.

    a) Determine the appropriate 'options group' for the new option, if any.
    b) Add the option to the file.

3) Parameters in the top level and mid level scripts may also need to be
   propogated to other, lower level, scripts.

4) The following steps are modifications to this module:
    a) Add a new parameter in the section 'Tracing configuration parameters'.
    b) Add new parameter to these methods: DumpGlobalVars(), ReadGlobalVars(),
       SetGlobalVars() and GetCBSPBinParams().
    c) If the new option is a parameter which can be read from a tracing
       parameter configuration file, then also add it to the methods ParseCfgFile()
       and SetGlobalAttr().

NOTE: To add a new simulator, code in this module needs to be modified.
Look for the string:  Types of kits
"""

#####################################################################################
#
# Global variables
#
# Global variables are saved in a pickle file when calling a new python script.
# This allows the globals to be passed between scripts.  If a pickle file
# exists, then assume we were called from another script.  Read in the global
# variables from the pickle file.
#
#####################################################################################

# Base name of the pickle file used to transfer data.
#
pickle_file_name = 'global.dat'

# Did we already read global variables from a pickle file?
#
read_pickle_vars = False

# Global variables
#
verbose = False
debug   = False

#####################################################################################
#
# Attributes which are constants used throughout the scripts.
#
#####################################################################################

# Location of the Python script directories within the Pin and SDE kits
#
pin_script_path = os.path.join('extras', 'pinplay', 'scripts')
sde_script_path = 'pinplay-scripts'

# Default file name for the application tracing configuration information
#
config_ext = '.cfg'
default_config_file = 'tracing' + config_ext

# File extension for PinPlay message files.
#
msgfile_ext = ''

# String used to differentiate the per instance cfg/status files from the 
# remaining file/dir names.
#
instance_ext = '.info'

# File extension for the status of the phases run by a script.
#
phase_status_ext = '.status'

# File extension for the simulator output log file.
#
sim_out_ext = '.sim.txt'

# File extension for the Sniper output file (i.e. text generated
# when Sniper runs).  See also 'sniper_result_dir'.
#
sniper_out_ext = '.sniper.txt'

# Base file name for CSV directories.
#
csv_dir_base = "csv_iter"

# Basename for the whole program pinball directories.
#
wp_dir_basename = 'whole_program'

# String added to a directory name when it is filtered by relogging.
#
relog_dir_str = '.relog'

# Location of Sniper result files. This is where the results from running
# Sniper are located, not the location where the output from running
# Sniper is stored. See also 'sniper_out_ext'.
#
sniper_result_dir = 'sniper_results'

# Char which is used as the separator between 'program_name' and 'input_name'
# when generating the Data/pp/lit dir names for a given tracing instance.  Set
# default value here.  It can be changed in config files or with a command line
# option.  
#
dir_separator = '.'

#####################################################################################
#
# Tracing configuration parameters
#
# These define paramters which describe the application being traced and how to trace it.
# Many of these tracing parameters are also saved/restored in the pickle file to pass
# them to the lower level scripts.
#
#####################################################################################

# Parameters from config file or command line options. Set to nonsense values
# here.  Defaults are set in the module 'cmd_options.py' when the option is
# defined.
#
param_section  = 'Parameters'
archsim_config_dir = ''
command        = ''
cutoff         = 0.0
epilog_length  = 0
focus_thread   = -1
input_name     = ''
maxk           = 0
mode           = ''
num_cores      = 0
num_proc       = 0
pintool        = ''
pin_options    = ''
pinplayhome    = ''
program_name   = ''
prolog_length  = 0
sdehome        = ''
simhome        = ''
simpoint_options = ''
slice_size     = 0
sniper_root    = ''
warmup_length  = 0

# Should the parameter 'program_name' be used in the whole program
# pinball directory name?
#
add_program_wp = False

# What weight should the BBV files be given when scaling BBV/LDV files.
# Default is 0.5, half BBV and half LDV.
#
combine = -1.0

# Should the files be compressed.
#
compressed = 'bzip2'

# Should the LIT FILES be cooperative of the default (per thread).
#
coop_lit = False

# Should the region pinballs be cooperative instead of the default (per thread).
#
coop_pinball = False

# Whole program pinballs collected on Windows, but remaining processing is being done on Linux.
#
cross_os = False

# Log file (pinball) name
#
log_file = ''

# MPI command line options
#
mpi_options = ''

# Do not use a focus thread.
#
no_focus_thread = False

# Name of the directory which contains the relogged whole program pinballs.
#
relog_dir = ''

# Boolean to see if the global data files should be saved instead
# of deleted after they are read.  Useful for debugging.
#
save_global = False

# List of global files which should be deleted when the script exits.
#
global_file_list = []

# Process to use for running a simulator.
#
processor = ''

# User defined directory containing the whole program pinballs.
#
whole_pgm_dir = ''

#####################################################################################
#
# Variables which are passed from script to script via pickle files, but which
# are not parameters which can be read from a tracing configuration file.
#
#####################################################################################

# The type of the simulator kit. Used by a script to pass along the kit
# type to another script.
#
sim_kit_type = None

# Should LDV files be combined with BBV files.
#
ldv = None

# Should  create/use global regions
#
global_regions = None

#####################################################################################

# Number of instructions in a CMPSim 'phase'.  To calculate the number of
# phases which should be given to the knob '-phaselen', divide the pinball
# icount by this value.
# 
instr_cmpsim_phase = 1000000

# List of 'blank' XML file components required to generate a traceinfo.xml file.
#
traceinfo_blank_files=['blank.traceinfo.DTD.xml', 'blank.traceinfo.header.xml', 'blank.traceinfo.footer.xml']

#####################################################################################
#
# Parameters for the rename.py script
#
#####################################################################################

app_version = ''
compiler_version = ''
platform = ''

#####################################################################################
#
# Values for the DrDebug scripts
#
#####################################################################################

# Minimum version of GDB which can be used with DrDebug.
#
gdb_base_version = '7.4'

# Always use these knobs when doing logging in the DrDebug scripts.
#
# For now, remove the knob '-log:whole_stack' until the logger is fixed so this knob works
# with apps using signals.
#
# drdebug_base_log_options = ' -log:region_id -log:whole_image -log:pages_early 1 -log:whole_stack -pinplay:max_threads 128'
drdebug_base_log_options = ' -log:region_id -log:whole_image -log:pages_early 1 '

# A file which contains GDB commands.
#
gdb_cmd_file = ''

#####################################################################################
#
# Values for CBSP scripts
#
#####################################################################################

# Name of CBSP (Cross Binary SimPoint) instance
#
cbsp_name = ''

# Use these knobs when logging with the CBSP scripts.
#
cbsp_base_log_options = ' -dcfg -dcfg:write_dcfg -dcfg:write_trace'

#####################################################################################
#
# Define types used in the scripts.
#
#####################################################################################

# What is architecture.
#
ARCH_INVALID = 0
ARCH_IA32 = 1
ARCH_INTEL64 = 2

# What type of application are we working with.
#
ST_MODE = 'st'         # This implictly means a single process
MT_MODE = 'mt'         # This implictly means a single process
MPI_MODE = 'mpi'       # MPI app, implictly means single threaded
MPI_MT_MODE = 'mpi_mt' # MPI app, multi-threaded
MP_MODE = 'mp'         # Multi-process, implictly means single threaded
MP_MT_MODE = 'mp_mt'   # Multi-process, Multi-threaded

# Relogging phases
#
RELOG_CODE_EXCLUDE = 0       # Relogging with code exclusion
RELOG_FOCUS        = 1       # Relogging with a focus thread
RELOG_NAME         = 2       # User defined name for relogging (user must specify logging knobs)
RELOG_NO_CLEANUP   = 3       # Relogging to remove cleanup instructions
RELOG_NO_INIT      = 4       # Relogging to remove initialization instructions
RELOG_NO_MPI_SPIN  = 5       # Relogging to remove MPI spin instructions
RELOG_NONE         = 6       # Not relogging
RELOG_NO_OMP_SPIN  = 7       # Relogging to remove OpenMP spin instructions

# Types of kits.
#
# NOTE: If you are adding a kit for a new simulator, then you need
# to add the type here.
#
UNKNOWN = -1
PINPLAY = 0
SDE     = 1
BRPRED  = 2
CMPSIM  = 3
X86NOAS = 4
X86     = 5
KEIKO   = 6

# What type of platform are we running on?
#
LINUX = 0
WIN_CYGWIN = 1
WIN_NATIVE = 2

#####################################################################################
#
# Define a unique string to identify each phase of the tracing process
#
#####################################################################################

phase_str = [  \
    'native_pure', \
    'native_pin', \
    'log_whole', \
    'filter_user_defn', \
    'filter_focus_thread', \
    'filter_init', \
    'filter_cleanup', \
    'filter_code_exclude', \
    'filter_OMP_spin', \
    'filter_MPI_spin', \
    'replay_whole', \
    'gen_BBV', \
    'Simpoint', \
    'relog_regions', \
    'replay_regions', \
    'LIT', \
    'traceinfo', \
    'CMPsim_regions', \
    'CMPsim_whole', \
    'pred_error', \
    'verify_LIT', \
    'sim_regions', \
    'sim_whole', \
    'imix_lit', \
    'imix_regions', \
    'imix_whole', \
    'check_code_exclude', \
    'sniper_regions', \
    'sniper_whole', \
    'cb_match' \
]

native_pure         = 0
native_pin          = 1
log_whole           = 2
filter_user_defn    = 3
filter_focus_thread = 4
filter_init         = 5
filter_cleanup      = 6
filter_code_exclude = 7
filter_OMP_spin     = 8
filter_MPI_spin     = 9
replay_whole        = 10
gen_BBV             = 11
Simpoint            = 12
relog_regions       = 13
replay_regions      = 14
LIT                 = 15
traceinfo           = 16
CMPsim_regions      = 17
CMPsim_whole        = 18
pred_error          = 19
verify_LIT          = 20
sim_regions         = 21
sim_whole           = 22
imix_lit            = 23
imix_regions        = 24
imix_whole          = 25
check_code_exclude  = 26
sniper_regions      = 27
sniper_whole        = 28
cb_match            = 29

def PhaseStr(phase):
    """
    Format a unique string for each phase.

    @param phase Phase for string

    @return Formatted string identifying phase
    """

    return '[%s]' % phase_str[phase]


#####################################################################################
#
# Class for global variables
#
#####################################################################################

class GlobalVar(object):

    """
    Contains methods to dump/save global data attributes.

    When one script invokes another script using a system call, the global
    variables are saved in a pickle file.  The name of the specific pickle file
    where the variables are stored is given as an option to the called script.
    When the called script starts, it then reads the global variables from this
    pickle file.
    """

    def DumpGlobalVars(self, pinplay=True):
        """
        Save global variables when using a system call to execute another
        tracing script.  It uses the pickle module to write out the global
        objects to be passed to the next script.

        Because some scripts are run concurrently, the pickle file name must be
        unique for each instance of method DumpGlobalVars().  As a result, this
        method returns the unique pickle file name it generates.

        NOTE: Methods DumpGlobalVars(), ReadGlobalVars() and SetGlobalVars() must
        all be identically modified when the objects in the pickle file are changed.

        Any script which calls this method, should also call RmGlobalFiles() to
        clean up any global files remaining when the script exits.

        Always dump a set of common parameters for both PinPlay/CBSP.  If run
        from a PinPlay script, also dump these parameters as well.  (default behavior)

        @param pinplay If False, dump only the CBSP params

        @return pickle file name
        """

        global global_file_list

        # Add a random number to the pickle file name.  If the file already
        # exists, then get different file name.  Only do this one time.
        #
        key = random.randint(0,32728)
        pickle_name = pickle_file_name + '.' + str(key)
        if os.path.isfile(pickle_name):
            key = random.randint(0,32728)
            pickle_name = pickle_file_name + '.' + str(key)

        try:
            pickle_file = open(pickle_name, 'wb+')
        except IOError:
            msg.PrintAndExit('Can\'t open pickle file: ' + pickle_name)

        # Add to the global list of files. This is used for cleanup when the
        # script exits.
        #
        global_file_list.append(pickle_file)

        try:
            # First dump the type of file
            #
            pickle.dump(pinplay, pickle_file)

            # Dump PinPlay and CBSP parameters
            #
            pickle.dump(add_program_wp, pickle_file)
            pickle.dump(compressed, pickle_file)
            pickle.dump(cross_os, pickle_file)
            pickle.dump(debug, pickle_file)
            pickle.dump(dir_separator, pickle_file)
            pickle.dump(num_cores, pickle_file)
            pickle.dump(pin_options, pickle_file)
            pickle.dump(pintool, pickle_file)
            pickle.dump(pinplayhome, pickle_file)
            pickle.dump(save_global, pickle_file)
            pickle.dump(sdehome, pickle_file)
            pickle.dump(verbose, pickle_file)

            # Dump PinPlay only parameters
            #
            if pinplay:
                pickle.dump(archsim_config_dir, pickle_file)
                pickle.dump(combine, pickle_file)
                pickle.dump(command, pickle_file)
                pickle.dump(coop_lit, pickle_file)
                pickle.dump(coop_pinball, pickle_file)
                pickle.dump(focus_thread, pickle_file)
                pickle.dump(input_name, pickle_file)
                pickle.dump(ldv, pickle_file)
                pickle.dump(global_regions, pickle_file)
                pickle.dump(log_file, pickle_file)
                pickle.dump(mode, pickle_file)
                pickle.dump(mpi_options, pickle_file)
                pickle.dump(msgfile_ext, pickle_file)
                pickle.dump(no_focus_thread, pickle_file)
                pickle.dump(num_proc, pickle_file)
                pickle.dump(processor, pickle_file)
                pickle.dump(program_name, pickle_file)
                pickle.dump(simhome, pickle_file)
                pickle.dump(simpoint_options, pickle_file)
                pickle.dump(sniper_root, pickle_file)
                pickle.dump(sim_kit_type, pickle_file)
        except (pickle.PicklingError):
            msg.PrintAndExit('Error writing pickle file: ' + pickle_name)
        pickle_file.close()

        return pickle_name

    def ReadGlobalVars(self, options):
        """
        If a pickle data file exists, use this method sets the global variables
        in the current instance from the pickle file.  This must be done AFTER
        all the config files are read.  This allows the global variables from
        the previous script to overwrite values read from the config file.

        NOTE: Methods DumpGlobalVars(), ReadGlobalVars() and SetGlobalVars() must
        all be identically modified when the objects in the pickle file are changed.

        @param options Options given on cmd line

        @return no return
        """

        global add_program_wp
        global archsim_config_dir
        global combine
        global command
        global compressed
        global coop_lit
        global coop_pinball
        global cross_os
        global cutoff
        global debug
        global dir_separator
        global epilog_length
        global focus_thread
        global input_name
        global ldv
        global global_regions
        global log_file
        global maxk
        global mode
        global mode
        global mpi_options
        global msgfile_ext
        global no_focus_thread
        global num_cores
        global num_proc
        global pin_options
        global pinplayhome
        global pintool
        global processor
        global program_name
        global prolog_length
        global save_global
        global sdehome
        global simhome
        global sim_kit_type
        global simpoint_options
        global slice_size
        global sniper_root
        global verbose
        global warmup_length

        # import pdb;  pdb.set_trace()
        if hasattr(options, 'verbose') and options.verbose:
            msg.PrintMsg('Global data file given on the command line: ' + str(hasattr(options, 'global_file')))
        if hasattr(options, 'global_file') and options.global_file:
            pickle_name = options.global_file      # Use name given in command line option
        else:
            pickle_name = pickle_file_name         # Use default name

        if hasattr(options, 'verbose') and options.verbose and \
           hasattr(options, 'global_file') and options.global_file:
            msg.PrintMsg('Reading from global data file:  ' + str(pickle_name))

        if os.path.isfile(pickle_name):
            try:
                pickle_file = open(pickle_name, 'rb')
            except IOError:
                msg.PrintAndExit('method config.ReadGlobalVars() can\'t open global data (pickle) file: ' + \
                    pickle_name)
            try:
                # First read the type of file
                #
                pinplay = pickle.load(pickle_file)

                # Read both PinPlay/CBSP parameters
                #
                add_program_wp  = pickle.load(pickle_file)
                compressed      = pickle.load(pickle_file)
                cross_os        = pickle.load(pickle_file)
                debug           = pickle.load(pickle_file)
                dir_separator   = pickle.load(pickle_file)
                num_cores       = pickle.load(pickle_file)
                pin_options     = pickle.load(pickle_file)
                pintool         = pickle.load(pickle_file)
                pinplayhome     = pickle.load(pickle_file)
                save_global     = pickle.load(pickle_file)
                sdehome         = pickle.load(pickle_file)
                verbose         = pickle.load(pickle_file)

                # Read PinPlay only parameters
                #
                if pinplay:
                    archsim_config_dir = pickle.load(pickle_file)
                    combine            = pickle.load(pickle_file)
                    command            = pickle.load(pickle_file)
                    coop_lit           = pickle.load(pickle_file)
                    coop_pinball       = pickle.load(pickle_file)
                    focus_thread       = pickle.load(pickle_file)
                    input_name         = pickle.load(pickle_file)
                    ldv                = pickle.load(pickle_file)
                    global_regions     = pickle.load(pickle_file)
                    log_file           = pickle.load(pickle_file)
                    mode               = pickle.load(pickle_file)
                    mpi_options        = pickle.load(pickle_file)
                    msgfile_ext        = pickle.load(pickle_file)
                    no_focus_thread    = pickle.load(pickle_file)
                    num_proc           = pickle.load(pickle_file)
                    processor          = pickle.load(pickle_file)
                    program_name       = pickle.load(pickle_file)
                    simhome            = pickle.load(pickle_file)
                    simpoint_options   = pickle.load(pickle_file)
                    sniper_root        = pickle.load(pickle_file)
                    sim_kit_type       = pickle.load(pickle_file)
            except:
                msg.PrintAndExit('Error reading pickle file: ' + pickle_name)

            pickle_file.close()
            self.read_pickle_vars = True

            # Once the global data is read from the file, delete it.  This is
            # to ensure other scripts don't read this by mistake.
            #
            if not save_global:
                os.remove(pickle_name)

        # Set these values in the 'options' object [returned from
        # optparse.parse_args()] because many locations in the code use the
        # config object to access parameters, while others use the config
        # global attributes to get them.  Only set them if the 'options' value
        # is the default (i.e. was not set on the command line) and the config
        # global attribute is not the default value.
        #
        if hasattr(options, 'add_program_wp')and options.add_program_wp == False and add_program_wp != False:
            options.add_program_wp = add_program_wp
        if hasattr(options, 'archsim_config_dir') and options.archsim_config_dir == '' and archsim_config_dir:
            options.archsim_config_dir = archsim_config_dir
        if hasattr(options, 'combine') and options.combine == -1.0 and combine != -1.0:
            options.combine = combine
        if hasattr(options, 'command') and options.command == '' and command:
            options.command = command
        if hasattr(options, 'input_name') and options.input_name == '' and input_name:
            options.input_name = input_name
        if hasattr(options, 'program_name') and options.program_name == '' and program_name:
            options.program_name = program_name
        if hasattr(options, 'cutoff') and options.cutoff == 0 and cutoff > 0:
            options.cutoff = cutoff
        if hasattr(options, 'cbsp_name') and options.cbsp_name == '' and cbsp_name:
            options.cbsp_name = cbsp_name
        if hasattr(options, 'coop_lit')and options.coop_lit == False and coop_lit != False:
            options.coop_lit = coop_lit
        if hasattr(options, 'coop_pinball')and options.coop_pinball == False and coop_pinball != False:
            options.coop_pinball = coop_pinball
        if hasattr(options, 'cross_os')and options.cross_os == False and cross_os != False:
            options.cross_os = cross_os
        # NOTE: 'bzip2' is the default value for option.compressed
        if hasattr(options, 'compressed') and options.compressed == 'bzip2' and compressed != 'bzip2':
            options.compressed = compressed
        if hasattr(options, 'debug') and options.debug == False and debug != False:
            options.debug = debug
        if hasattr(options, 'dir_separator') and options.dir_separator == '' and dir_separator:
            options.dir_separator = dir_separator
        if hasattr(options, 'epilog_length') and options.epilog_length == 0 and epilog_length > 0:
            options.epilog_length = epilog_length
        if hasattr(options, 'focus_thread') and options.focus_thread == -1 and focus_thread > -1:
            options.focus_thread = focus_thread
        if hasattr(options, 'simhome') and options.simhome == '' and simhome:
            options.simhome = simhome
        if hasattr(options, 'simpoint_options') and options.simpoint_options == '' and simpoint_options:
            options.simpoint_options = simpoint_options
        if hasattr(options, 'pinplayhome') and options.pinplayhome == '' and pinplayhome:
            options.pinplayhome = pinplayhome
        if hasattr(options, 'pintool') and options.pintool == '' and pintool:
            options.pintool = pintool
        if hasattr(options, 'pin_options') and options.pin_options == '' and pin_options:
            options.pin_options = pin_options
        if hasattr(options, 'prolog_length') and options.prolog_length == 0 and prolog_length > 0:
            options.prolog_length = prolog_length
        if hasattr(options, 'maxk') and options.maxk == 0 and maxk > 0:
            options.maxk = maxk
        if hasattr(options, 'ldv') and not options.ldv and ldv:
            options.ldv = ldv
        if hasattr(options, 'global_regions') and not options.global_regions and global_regions:
            options.global_regions = global_regions
        if hasattr(options, 'log_file') and not options.log_file and log_file:
            options.log_file = log_file
        if hasattr(options, 'mode') and options.mode == '' and mode:
            options.mode = mode
        if hasattr(options, 'mpi_options') and options.mpi_options == '' and mpi_options:
            options.mpi_options = mpi_options
        if hasattr(options, 'msgfile_ext') and options.msgfile_ext == '' and msgfile_ext:
            options.msgfile_ext = msgfile_ext
        if hasattr(options, 'no_focus_thread') and options.no_focus_thread == False and no_focus_thread != False:
            options.no_focus_thread = no_focus_thread
        if hasattr(options, 'num_cores') and options.num_cores == 0 and num_cores > 0:
            options.num_cores = num_cores
        if hasattr(options, 'num_proc') and options.num_proc == 0 and num_proc > 0:
            options.num_proc = num_proc
        if hasattr(options, 'processor') and options.processor == '' and processor:
            options.processor = processor
        if hasattr(options, 'save_global') and options.save_global == False and save_global != False:
            options.save_global = save_global
        if hasattr(options, 'sdehome') and options.sdehome == '' and sdehome:
            options.sdehome = sdehome
        if hasattr(options, 'sniper_root') and options.sniper_root == '' and sniper_root:
            options.sniper_root = sniper_root
        if hasattr(options, 'slice_size') and options.slice_size == 0 and slice_size > 0:
            options.slice_size = slice_size
        if hasattr(options, 'verbose') and options.verbose == False and verbose != False:
            options.verbose = verbose
        if hasattr(options, 'warmup_length') and options.warmup_length == 0 and warmup_length > 0:
            options.warmup_length = warmup_length

        if hasattr(options, 'whole_pgm_dir') and options.whole_pgm_dir == '' and whole_pgm_dir:

            # If deleting files, then we do NOT want to set the option
            # 'whole_pgm_dir' from the per instance configuration file.
            # This is required in order to ensure the parameter from the old
            # cfg file is not propogated to the new tracing instance which will
            # be generated after the old files are deleted.
            #
            if not hasattr(options, 'delete') and not hasattr(options, 'delete_all'):
                    options.whole_pgm_dir = whole_pgm_dir
            elif (hasattr(options, 'delete') and not options.delete) and \
                 (hasattr(options, 'delete_all') and not options.delete_all):
                    options.whole_pgm_dir = whole_pgm_dir

        # For the script rename.py.  These are not passed via the global pickle file,
        # only used as parameters which can be read from a tracing configuration file.
        #
        global app_version
        global compiler_version
        global platform

        # import pdb ; pdb.set_trace()
        if hasattr(options, 'app_version') and options.app_version == '' and app_version:
            options.app_version = app_version
        if hasattr(options, 'compiler_version') and options.compiler_version == '' and compiler_version:
            options.compiler_version = compiler_version
        if hasattr(options, 'platform') and options.platform == '' and platform:
            options.platform = platform

    def SetGlobalVars(self, options):
        """
        Set the config global attributes (i.e. global variables) in this module.

        If this method was called from another script, then the global
        variables were already read from a pickle file.  Otherwise, this script
        was run from the command line and we will set global variables from
        options.

        NOTE: Methods DumpGlobalVars(), ReadGlobalVars() and SetGlobalVars() must
        all be identically modified when the objects in the pickle file are changed.

        @param options Options given on cmd line

        @return no return
        """

        global add_program_wp
        global archsim_config_dir
        global cbsp_name
        global combine
        global coop_lit
        global command
        global compressed
        global coop_pinball
        global cross_os
        global cutoff
        global debug
        global dir_separator
        global epilog_length
        global focus_thread
        global input_name
        global ldv
        global global_regions
        global log_file
        global maxk
        global mode
        global mpi_options
        global msgfile_ext
        global no_focus_thread
        global num_cores
        global num_proc
        global pin_options
        global pinplayhome
        global pintool
        global processor
        global program_name
        global prolog_length
        global save_global
        global sdehome
        global simhome
        global simpoint_options
        global slice_size
        global verbose
        global sniper_root
        global warmup_length
        global whole_pgm_dir

        # import pdb ; pdb.set_trace()
        if not read_pickle_vars:

            # Since there isn't a pickle file, these variables in this module
            # have not been set from options given to a previous script.  If
            # the globals are already true, this came from the config file.
            # However, the options override the config file, so set them now.
            #
            if hasattr(options, 'verbose') and options.verbose:
                verbose = options.verbose
            if hasattr(options, 'debug') and options.debug:
                debug  = options.debug

        # If these parameters were given on the command line (i.e. they are not
        # the default value in 'options'), then to set the config global
        # attributes to this value.
        #
        if hasattr(options, 'add_program_wp') and options.add_program_wp != False:
            add_program_wp = options.add_program_wp
        if hasattr(options, 'combine') and options.combine != -1.0:
            combine = options.combine
        if hasattr(options, 'command') and options.command:
            command = options.command
        if hasattr(options, 'input_name') and options.input_name:
            input_name = options.input_name
        if hasattr(options, 'program_name') and options.program_name:
            program_name = options.program_name
        if hasattr(options, 'ldv') and options.ldv:
            ldv = options.ldv
        if hasattr(options, 'global_regions') and options.global_regions:
            global_regions = options.global_regions
        if hasattr(options, 'log_file') and options.log_file:
            log_file = options.log_file
        if hasattr(options, 'mode') and options.mode:
            mode = options.mode
        if hasattr(options, 'archsim_config_dir') and options.archsim_config_dir:
             archsim_config_dir = options.archsim_config_dir
        if hasattr(options, 'cbsp_name') and options.cbsp_name:
             cbsp_name = options.cbsp_name
        if hasattr(options, 'cutoff') and options.cutoff > 0:
            cutoff = options.cutoff
        if hasattr(options, 'coop_lit') and options.coop_lit != False:
            coop_lit = options.coop_lit
        if hasattr(options, 'coop_pinball') and options.coop_pinball != False:
            coop_pinball = options.coop_pinball
        if hasattr(options, 'cross_os') and options.cross_os != False:
            cross_os = options.cross_os
        if hasattr(options, 'compressed') and options.compressed != 'bzip2':
            compressed = options.compressed
        if hasattr(options, 'dir_separator') and options.dir_separator:
             dir_separator = options.dir_separator
        if hasattr(options, 'focus_thread') and options.focus_thread >= 0:
             focus_thread = options.focus_thread
        if hasattr(options, 'epilog_length') and options.epilog_length > 0:
             epilog_length = options.epilog_length
        if hasattr(options, 'simhome') and options.simhome:
             simhome = options.simhome
        if hasattr(options, 'simpoint_options') and options.simpoint_options:
             simpoint_options = options.simpoint_options
        if hasattr(options, 'pinplayhome') and options.pinplayhome:
             pinplayhome = options.pinplayhome
        if hasattr(options, 'pintool') and options.pintool:
             pintool = options.pintool
        if hasattr(options, 'pin_options') and options.pin_options:
             pin_options = options.pin_options
        if hasattr(options, 'prolog_length') and options.prolog_length > 0:
             prolog_length = options.prolog_length
        if hasattr(options, 'maxk') and options.maxk > 0:
             maxk = options.maxk
        if hasattr(options, 'mpi_options') and options.mpi_options:
             mpi_options = options.mpi_options
        if hasattr(options, 'msgfile_ext') and options.msgfile_ext:
            msgfile_ext = options.msgfile_ext
        if hasattr(options, 'no_focus_thread') and options.no_focus_thread != False:
            no_focus_thread = options.no_focus_thread
        if hasattr(options, 'num_cores') and options.num_cores > 0:
             num_cores = options.num_cores
        if hasattr(options, 'num_proc') and options.num_proc > 1:
             num_proc = options.num_proc
        if hasattr(options, 'processor') and options.processor:
             processor = options.processor
        if hasattr(options, 'save_global') and options.save_global != False:
             save_global = options.save_global
        if hasattr(options, 'sdehome') and options.sdehome:
             sdehome = options.sdehome
        if hasattr(options, 'sniper_root') and options.sniper_root:
             sniper_root = options.sniper_root
        if hasattr(options, 'slice_size') and options.slice_size > 0:
             slice_size = options.slice_size
        if hasattr(options, 'warmup_length') and options.warmup_length > 0:
             warmup_length = options.warmup_length
        if hasattr(options, 'whole_pgm_dir') and options.whole_pgm_dir:
             whole_pgm_dir = options.whole_pgm_dir

        # For the script rename.py.  These are not passed via the global pickle file,
        # only used as parameters which can be read from a tracing configuration file.
        #
        global app_version
        global compiler_version
        global platform

        if hasattr(options, 'app_version') and options.app_version:
             app_version = options.app_version
        if hasattr(options, 'compiler_version') and options.compiler_version:
             compiler_version = options.compiler_version
        if hasattr(options, 'platform') and options.platform:
             platform = options.platform

    def RmGlobalFiles(self, options):
        """
        Clean up any global data files which may still exist.  For instance,
        if there has been an error.

        If the user just wants to list the instructions, then don't delete the
        file becaused it's assumed the user will be using another method (such
        as NetBatch) to run the commands which may need the file.  Thus the
        global data file must still be around when the scripts are run.  Also
        save them if the user explicitly requests they not be deleted via
        '--save_global'.

        This should be called at the end of each script which calls DumpGlobalVars().

        @param options Options given on cmd line

        @return no return
        """

        # Import util.py here instead of at the top level to keep from having recusive module includes, as util
        # imports config.
        #
        import util

        if not (hasattr(options, 'list') and options.list) and not save_global:
            platform = util.Platform()
            if platform == LINUX or platform == WIN_CYGWIN:
                rm_cmd = 'rm -f '
            else:
                # Running on native Windows.
                # Can always use 'del' here because only files are being
                # deleted, not directories.  Must add 'rmdir' if code is
                # changed to include deleting directories.
                #
                rm_cmd = 'del /q /s '
            for glob_file in global_file_list:
                if os.path.isfile(glob_file.name):
                    cmd = rm_cmd + glob_file.name
                    p = subprocess.Popen(cmd, shell=True)
                    p.communicate()


#####################################################################################
#
# Class for parsing and setting configuration file variables
#
#####################################################################################

class ConfigClass(object):
    """
    Tracing configuration parameters are read in from a file, if it exists.

    Any values for parameters from the command line override the values read
    from the tracing configuration file.
    """

    # File parser object used to parse all configuration files for a PinPlay tracing
    # instance.
    #
    import ConfigParser
    parser = ConfigParser.ConfigParser()

    def GetVarStr(self, section, name, parser):
        """
        Get a string parameter from a cfg file parser.

        @param section Section in the configuration file
        @param name Name of variable to read

        @return string with parameter
        @return '' if not found or an error occurs
        """

        value = ''
        if parser.has_option(section, name):
            try:
                value = parser.get(section, name)
            except ValueError:
                pass

        return value

    def GetVarBool(self, section, name, parser):
        """
        Get a boolean parameter from a cfg file parser.

        @param section Section in the configuration file
        @param name Name of variable to read

        @return boolean
        @return false if not found or an error occurs
        """

        value = False
        if parser.has_option(section, name):
            try:
                value = parser.getboolean(section, name)
            except ValueError:
                pass

        return value

    def GetVarInt(self, section, name, parser):
        """
        Get a integer parameter from a cfg file parser.

        @param section Section in the configuration file
        @param name Name of variable to read

        @return integer
        @return 0 if not found or an error occurs
        """

        value = 0
        if parser.has_option(section, name):
            try:
                value = parser.getint(section, name)
            except ValueError:
                pass

        return value

    def GetVarFloat(self, section, name, parser):
        """
        Get a float parameter from a cfg file parser.

        @param section Section in the configuration file
        @param name Name of variable to read

        @return floating point value
        @return 0.0 if not found or an error occurs
        """

        value = 0.0
        if parser.has_option(section, name):
            try:
                value = parser.getfloat(section, name)
            except ValueError:
                pass

        return value

    def PrintCfg(self, parser):
        """
        For debugging, print out the sections & values.

        @param parser Configuration file parser

        @return no return
        """

        for section_name in parser.sections():
            print 'Section:', section_name
            # print '  Options:', parser.options(section_name)
            for name, value in parser.items(section_name):
                print '  %s = %s' % (name, value)
            print

    def ParseCfgFile(self, parser):
        """
        Read the parameters from a tracing configuration file parser.

        Need to add new params to this function.

        @param parser Configuration file parser

        @return dictionary of parameters
        """

        # See if the config file has the section 'Parameters'.
        #
        if parser.has_section(param_section):
            section = param_section
        else:
            # No parameter section so no parameters to read.
            #
            msg.PrintAndExit('The configuration file does not have the'
                ' required section \'Parameters\'.\n'
                'Add a section with the header \'[Parameters]\'.')

        # import pdb ; pdb.set_trace()
        params = {}
        params['app_version']        = self.GetVarStr(section,   'app_version', parser)
        params['archsim_config_dir'] = self.GetVarStr(section,   'archsim_config_dir', parser)
        params['cbsp_name']          = self.GetVarStr(section,   'cbsp_name', parser)
        params['command']            = self.GetVarStr(section,   'command', parser)
        params['compiler_version']   = self.GetVarStr(section,   'compiler_version', parser)
        params['cutoff']             = self.GetVarFloat(section, 'cutoff', parser)
        params['debug']              = self.GetVarBool(section,  'debug', parser)
        params['epilog_length']      = self.GetVarInt(section,   'epilog_length', parser)
        params['input_name']         = self.GetVarStr(section,   'input_name', parser)
        params['log_file']           = self.GetVarStr(section,   'log_file', parser)
        params['log_file']           = self.GetVarStr(section,   'pinball', parser)   # Two names for the same parameter
        params['maxk']               = self.GetVarInt(section,   'maxk', parser)
        params['mode']               = self.GetVarStr(section,   'mode', parser)
        params['mpi_options']        = self.GetVarStr(section,   'mpi_options', parser)
        params['no_focus_thread']    = self.GetVarBool(section,  'no_focus_thread', parser)
        params['num_cores']          = self.GetVarInt(section,   'num_cores', parser)
        params['num_proc']           = self.GetVarInt(section,   'num_proc', parser)
        params['pin_options']        = self.GetVarStr(section,   'pin_options', parser)
        params['pinplayhome']        = self.GetVarStr(section,   'pinplayhome', parser)
        params['pintool']            = self.GetVarStr(section,   'pintool', parser)
        params['platform']           = self.GetVarStr(section,   'platform', parser)
        params['processor']          = self.GetVarStr(section,   'processor', parser)
        params['program_name']       = self.GetVarStr(section,   'program_name', parser)
        params['prolog_length']      = self.GetVarInt(section,   'prolog_length', parser)
        params['relog_dir']          = self.GetVarStr(section,   'relog_dir', parser)
        params['sdehome']            = self.GetVarStr(section,   'sdehome', parser)
        params['simhome']            = self.GetVarStr(section,   'simhome', parser)
        params['simpoint_options']   = self.GetVarStr(section,  'simpoint_options', parser)
        params['slice_size']         = self.GetVarInt(section,   'slice_size', parser)
        params['sniper_root']        = self.GetVarStr(section,   'sniper_root', parser)
        params['verbose']            = self.GetVarBool(section,  'verbose', parser)
        params['warmup_length']      = self.GetVarInt(section,   'warmup_length', parser)
        params['whole_pgm_dir']      = self.GetVarStr(section,   'whole_pgm_dir', parser)

        # Treat the config global attribute value 'dir_separator' differently than the
        # rest of the values.  We always need this to to be set via one of
        # these methods:
        #   1) the default value in the config module
        #   2) parameter given in a tracing configuration file
        #   3) parameter given on command line option
        #
        # If it's not given in the config file which is currently being read,
        # then do NOT set it to the default value returned by GetVarStr ('').
        # If dir_separator is already set in the global config we don't want to
        # overwrite it with the default value ''.
        #
        # import pdb ; pdb.set_trace()
        cfg_dir_separator = self.GetVarStr(section,   'dir_separator', parser)
        if cfg_dir_separator:
            params['dir_separator'] = cfg_dir_separator

        # import pdb ; pdb.set_trace()

        # Can't use self.GetVarInt() for attribute focus_thread, because the
        # default return value from this method when a parameter is found is 0.
        # However, 0 is a valid focus_thread.  Hence, need the following code
        # to set focus_thread to -1 if it's not a parameter in the config file.
        #
        focus_thread = -1
        if parser.has_option(section, 'focus_thread'):
            try:
                params['focus_thread'] = parser.getint(section, 'focus_thread')
            except ValueError:
                pass # Ignore error

        # Can't use self.GetFloat() for attribute combine, because the default
        # return value from this method when a parameter is not found is 0.0.
        # However, 0.0 is a valid value for the attribute.  Hence, only set
        # the attribute if it's in the tracing config file.
        #
        if parser.has_option(section, 'combine'):
            try:
                params['combine'] = parser.getfloat(section, 'combine')
            except ValueError:
                pass # Ignore error

        # Can't just use self.GetVarStr() for attribute compressed, because the
        # default return value from this method is ''.  However, the default
        # value for this parameter is 'bzip2'.  Hence, only set the attribute
        # if it's in the tracing config file.
        #
        if parser.has_option(section, 'compressed'):
            try:
                params['compressed'] = parser.get(section, 'compressed')
            except ValueError:
                pass # Ignore error

        if debug:
            self.PrintCfg(parser)

        return params


    def SetGlobalAttr(self, params):
        """
        Set the config global attributes from a dictionary of parameters/values.

        If an attribute isn't given, then set it to the appropriate default value.

        Need to add new params to this function.

        @param params dictionary containing parameters

        @return no return
        """

        global app_version
        global archsim_config_dir
        global cbsp_name
        global combine
        global command
        global compiler_version
        global compressed
        global cutoff
        global debug
        global dir_separator
        global epilog_length
        global focus_thread
        global input_name
        global log_file
        global maxk
        global mode
        global mpi_options
        global no_focus_thread
        global num_cores
        global num_proc
        global pin_options
        global pinplayhome
        global pintool
        global platform
        global processor
        global program_name
        global prolog_length
        global relog_dir
        global sdehome
        global simhome
        global simpoint_options
        global slice_size
        global sniper_root
        global verbose
        global warmup_length
        global whole_pgm_dir

        # Chose an appropriate default value based on the type of the global attribute
        #
        # import pdb ; pdb.set_trace()
        app_version        = params.get('app_version', '')
        archsim_config_dir = params.get('archsim_config_dir', '')
        cbsp_name          = params.get('cbsp_name', '')
        command            = params.get('command', '')
        compiler_version   = params.get('compiler_version', '')
        compressed         = params.get('compressed', 'bzip2')   # bzip2 is the default value for compression
        cutoff             = params.get('cutoff', 0.0)
        debug              = params.get('debug', False)
        epilog_length      = params.get('epilog_length', 0)
        input_name         = params.get('input_name', '')
        log_file           = params.get('log_file', '')
        log_file           = params.get('pinball', '')   # Two names for the same parameter
        maxk               = params.get('maxk', 0)
        mode               = params.get('mode', '')
        mpi_options        = params.get('mpi_options', '')
        no_focus_thread    = params.get('no_focus_thread', False)
        num_cores          = params.get('num_cores', 0)
        num_proc           = params.get('num_proc', 0)
        pin_options        = params.get('pin_options', '')
        pinplayhome        = params.get('pinplayhome', '')
        pintool            = params.get('pintool', '')
        platform           = params.get('platform', '')
        processor          = params.get('processor', '')
        program_name       = params.get('program_name', '')
        prolog_length      = params.get('prolog_length', 0)
        relog_dir          = params.get('relog_dir', '')
        sdehome            = params.get('sdehome', '')
        simhome            = params.get('simhome', '')
        simpoint_options   = params.get('simpoint_options', '')
        slice_size         = params.get('slice_size', 0)
        sniper_root        = params.get('sniper_root', '')
        verbose            = params.get('verbose', False)
        warmup_length      = params.get('warmup_length', 0)
        whole_pgm_dir      = params.get('whole_pgm_dir', '')

    def GetCfgFile(self, cfg_file, parser):
        """
        If the config file exists, then parse it to get the parameters from the
        file.

        It's OK if the default or instance configuration files don't exist.

        @param cfg_file Configuration file to parse
        @param parser Configuration file parser

        @return dictionary of params from config file
        """

        # import pdb ; pdb.set_trace()
        params = {}
        if os.path.isfile(cfg_file):
            try:
                parser.read(cfg_file)
            except ConfigParser.MissingSectionHeaderError:
                msg.PrintAndExit('This configuration file does not have any'
                    ' sections.\nIt must at least contain the section '
                    '\'Parameters\'.\n' '   ' + cfg_file)
            except ConfigParser.ParsingError:
                msg.PrintAndExit('There was an error parsing the configuration file.\n' + \
                    '   ' + cfg_file)
            params = self.ParseCfgFile(parser)
        else:
            if cfg_file != default_config_file and \
               cfg_file != self.GetInstanceFileName(config_ext):
                    msg.PrintAndExit('Configuration file does not exist. '
                        '   Please double check the file name.\n' '   ' + cfg_file)

        return params

    def ParamErrMsg(self, string):
        """
        Print msg indicating user must define a parameter.
        """

        msg.PrintAndExit('Required parameter \'%s\' not found.\n'
            'It must be defined in order to run the script.  Use either the option\n'
            '--%s or add the parameter to the tracing configuration file.' % (string, string))

    def CheckRequiredParameter(self, var):
        """
        Check to make sure a config global attribute has been initialized.

        @param var Parameter to check

        @return Always return True as script exits with error if not found
        """

        if eval(var) == '':
            self.ParamErrMsg(var)

        return True

    def Check2RequiredParameters(self):
        """
        Check to make sure the two basic, required variables have been initialized.

        @return Always return True as script exits with error if not found
        """

        self.CheckRequiredParameter('program_name')
        self.CheckRequiredParameter('input_name')

        return True

    def Check4RequiredParameters(self):
        """
        Check to make sure the two basic, required variables and two additional variables
        required for logging have been initialized.

        @return Always return True as script exits with error if not found
        """

        self.Check2RequiredParameters()
        self.CheckRequiredParameter('command')
        self.CheckRequiredParameter('mode')

        return True

    def SetPerBinParams(self, options):
        """
        Explicitly set a subset of config global attributes to specific values
        for each binary in CBSP.

        When running CBSP, for some phases config global attributes need to be
        explicitly set to a different value for each binary. This ensures the
        attributes contain the correct values before they are written out as
        part of a global data file (pickle file).

        This is only used for CBSP, not PinPlay.  Since PinPlay only works with
        one binary, there's no need to change the attribute once it's set from
        command line options and config files.

        @return no return value
        """

        global pin_options

        if hasattr(options, 'pin_options') and options.pin_options:
            pin_options = options.pin_options


    def ClearPerBinParams(self):
        """
        Explicitly clear a subset of config global attributes for each binary in CBSP.

        When running CBSP, for some phases config global attributes should not be set for
        each binary.

        This is only used for CBSP, not PinPlay.  Since PinPlay only works with
        one binary, there's no need to change the attribute once it's set from
        command line options and config files.

        @return no return value
        """

        global pin_options

        pin_options = ''


    def GetCBSPBinParams(self, params, options):
        """
        Generate an object of type optparse.Value which contains the required
        parameters for running CBSP on a binary.

        This method returns an object which is the same type as 'options'
        which is returned by optparse.parse_args().  This allows the object to
        be used in the same way as 'options' returned by parse_args().

        Parameters can be defined in either config files or via command line
        options.

        @param params Dictionary with parameters from all master CBSP config
                      files & and one binary cfg file

        @param options Options given on cmd line

        @return object of type optparse.Value which contains CBSP parameters
        """

        def get_param(key, required):
            """
            Get the key from either 'params' or 'options'.

            Command line options over-ride config file parameters,
            so check in 'options' last.

            Exit script with appropriate error msg if not found.

            @param key Desired parameter
            @param required Boolean to determine if should fail if param not found

            @return value associated with key
            """

            value = ''
            if params.has_key(key) and params[key]:
                value = params[key]
            if hasattr(options, key) and getattr(options, key):
                value = getattr(options, key)
            if required and not value:
                self.ParamErrMsg(key)

            return value

        global cbsp_name
        global cross_os
        global debug
        global epilog_length
        global mode
        global maxk
        global num_cores
        global pinplayhome
        global pintool
        global prolog_length
        global save_global
        global sdehome
        global slice_size
        global warmup_length

        # Use the same object that optparse returns for 'options'
        #
        values = optparse.Values()

        # Required parameters
        #
        setattr(values, 'cbsp_name', get_param('cbsp_name', True))
        setattr(values, 'command', get_param('command', True))
        setattr(values, 'input_name', get_param('input_name', True))
        setattr(values, 'mode', get_param('mode', True))
        setattr(values, 'program_name', get_param('program_name', True))

        # Optional parameters which do NOT have config global attributes set.
        # These parameters are only used in the high level scripts, not passed
        # via global data files to lower level scripts.
        #
        setattr(values, 'add_program_wp', get_param('add_program_wp', False))
        setattr(values, 'lit_options', get_param('lit_options', False))
        setattr(values, 'log_options', get_param('log_options', False))
        setattr(values, 'pin_options', get_param('pin_options', False))
        setattr(values, 'replay_options', get_param('replay_options', False))
        setattr(values, 'simpoint_options', get_param('simpoint_options', False))

        # Optional parameters which have config global attributes set.
        # These parameters are passed via global data files to lower level scripts.
        #
        setattr(values, 'cross_os', get_param('cross_os', False))
        setattr(values, 'debug', get_param('debug', False))
        setattr(values, 'epilog_length', get_param('epilog_length', False))
        setattr(values, 'list', get_param('list', False))
        setattr(values, 'maxk', get_param('maxk', False))
        setattr(values, 'num_cores', get_param('num_cores', False))
        setattr(values, 'pinplayhome', get_param('pinplayhome', False))
        setattr(values, 'pintool', get_param('pintool', False))
        setattr(values, 'prolog_length', get_param('prolog_length', False))
        setattr(values, 'save_global', get_param('save_global', False))
        setattr(values, 'sdehome', get_param('sdehome', False))
        setattr(values, 'slice_size', get_param('slice_size', False))
        setattr(values, 'warmup_length', get_param('warmup_length', False))

        # Now set config global attributes for the required parameters.
        #
        # NOTE: Don't forget to declare the attribute being set
        #       as global at the begining of this method.
        #
        cbsp_name = values.cbsp_name
        cross_os = values.cross_os
        debug = values.debug
        epilog_length = values.epilog_length
        mode = values.mode
        maxk = values.maxk
        num_cores = values.num_cores
        pinplayhome = values.pinplayhome
        pintool = values.pintool
        prolog_length = values.prolog_length
        save_global = values.save_global
        sdehome = values.sdehome
        slice_size = values.slice_size
        warmup_length = values.warmup_length

        # If parameter cbsp_name was only defined in the binary config file,
        # and not in the master CBSP config file, then need to set the
        # attribute in options (which came from master CBSP config file).
        #
        # Parameters set in binary config files overwrite params defined in 
        # master CBSP config files.
        #
        if values.cbsp_name:
            setattr(options, 'cbsp_name', values.cbsp_name)

        return values

    def GetAllCBSPBinParams(self, options):
        """
        Get a set of parameters for each one of the CBSP binaries.

        For each binary, look first in all the master CBSP config files, then cfg file
        for each binary and finally the command line options.

        @param options Options given on cmd line

        @return list of objects which contains parameters for binaries
        """

        # Import util.py here instead of at the top level to keep from having recusive module includes, as util
        # imports config.
        #
        import util

        # CBSP needs to always use the parameter 'add_program_wp', so add it
        # here to both options and config global attributes.
        #
        # import pdb;  pdb.set_trace()
        setattr(options, 'add_program_wp', True)
        add_program_wp = True

        # Process each binary defined in the config files given in 'cbsp_cfgs'.
        #
        bin_options = []
        for bin_cfg in options.cbsp_cfgs:

            # Want to parse the master CBSP config files before each binary
            # config file.  This allows parameters in the binary file to
            # overwrite ones defined in the CBSP master files.
            #
            # import pdb;  pdb.set_trace()
            cfg_files = []
            if hasattr(options, 'config_file'):
                cfg_files = list(options.config_file)
            cfg_files.append(bin_cfg)

            # Get parameters for binary from all config files
            #
            params = {}
            parser = ConfigParser.ConfigParser()
            for c_file in cfg_files:
                if hasattr(options, 'verbose') and options.verbose:
                    msg.PrintMsg('Reading config file: ' + c_file)
                params.update(self.GetCfgFile(c_file, parser))

            # Get parameters for this binary in an optparse.Values instance.
            # This is the same type of object as 'options' which is returned by
            # optparse.parse_args().  This allows the object to be used in the
            # same way as 'options' returned by parse_args().
            #
            # import pdb;  pdb.set_trace()
            opts = self.GetCBSPBinParams(params, options)
            util.AddMethodcbsp(opts)
            setattr(opts, 'config_file', cfg_files)
            bin_options.append(opts)

        return bin_options


    def FobidErrMsg(self, string, param, char):
        """
        Print a error msg about forbidden characters.

        @param string String with name of parameter which has a problem
        @param param Parameter with the problem
        @param char Illegal char in the parameter

        @return no return
        """

        msg.PrintAndExit('Parameter \'%s\' (%s) has the forbidden character \'%s\'.\n'
            'This is not allowed when using SDE/PinPlay. Traces in GTR cannot have these chars.' % \
            (string, param, char))

    def CheckForbiddenChar(self):
        """
        Check to see if several parameters contain one of the 'forbidden' chars, '.' or '_'.

        @return no return
        """

        if program_name.find('_') != -1:
            self.FobidErrMsg('program_name', program_name, '_')
        if program_name.find('.') != -1:
            self.FobidErrMsg('program_name', program_name, '.')
        if input_name.find('_') != -1:
            self.FobidErrMsg('input_name', input_name, '_')
        if input_name.find('.') != -1:
            self.FobidErrMsg('input_name', input_name, '.')

    def GetCfgGlobals(self, options, required_vars):
        """
        Set the config global attributes from all the different methods used
        to get parameters.

        First, read in all tracing configuration files, if any exist, and set
        the attributes with these parameters.  Then read in the pickle file, if
        it exists, to set attributes.  Finally, use the command line parameters
        in 'options' to set the attributes.  This method does NOT require a
        configuration file or a pickle file.

        If boolean 'required_vars' is true, then check to make sure the
        required variables are defined.

        @param options Options given on cmd line
        @param required_vars Boolean to indicate if method should check for required parameters

        @return no return
        """

        # Put the tracing configuration parameters from all the cfg files given
        # on the command line into the dictionary 'params'.
        #
        # import pdb ; pdb.set_trace()
        params = {}
        if hasattr(options, 'config_file') and options.config_file:
            for cfg_file in options.config_file:
                if os.path.isfile(cfg_file):
                    if hasattr(options, 'verbose') and options.verbose:
                        msg.PrintMsg('Reading config file: ' + cfg_file)
                    params.update(self.GetCfgFile(cfg_file, self.parser))
                else:
                    if cfg_file != default_config_file and \
                       cfg_file != self.GetInstanceFileName(config_ext):
                            msg.PrintAndExit('Configuration file does '
                                'not exist.  Please double check the file name.\n' + \
                                '   ' + cfg_file)

        # Set the global attributes with the current contents of 'param'.  Need
        # to do this now in order for 'options' to be populated with the
        # current parameters _before_ getting tracing paramters from the 'per
        # instance' tracing configuration file.  Some of the parameters in
        # options may be used in parsing the per instance file.
        #
        self.SetGlobalAttr(params)

        # Now get tracing parameters from the 'per instance' tracing configuration file.
        #
        # import pdb ; pdb.set_trace()
        params.update(self.GetCfgFile(self.GetInstanceFileName(config_ext), self.parser))

        # Set the config global attributes based on the final set of parameters.
        #
        self.SetGlobalAttr(params)

        # If this script was called from another script, read in any global
        # variables set in the previous script.  This is done after reading the
        # config file. This allows the global variables passed from the calling
        # script (in pickle file) to override any parameters set in the config
        # file(s).
        #
        gv = GlobalVar()
        gv.ReadGlobalVars(options)

        # Set the global variables using the command line options given when
        # this script was run.  These have the highest precedence and over-ride
        # any parameters already set.
        #
        # import pdb ; pdb.set_trace()
        gv.SetGlobalVars(options)

        # If needed, check to see if required variables have been defined. 
        #
        if required_vars:
            if hasattr(options, 'log') and options.log:
                # All 4 are only required if logging.
                #
                self.Check4RequiredParameters()
            else:
                # Otherwise, only 2 are required.
                #
                self.Check2RequiredParameters()

    def GetPPInstanceName(self, options=None):
        """
        Generate the basename for the PinPoint per instance files.

        The instance name identifies this specific tracing instance.  It is
        derived from: program_name and input_name.  Using these parameters as
        the file name ensures each tracing instance will have a unique file for
        recording parameters specific to this specific instance.

        @return string with PP basename
        """

        name = ''
        if options and hasattr(options, 'program_name') and options.program_name:
            name = options.program_name + dir_separator + options.input_name
        else:
            if program_name:
                name = program_name + dir_separator + input_name

        return name

    def GetCBSPInstanceName(self, options=None):
        """
        Generate the basename for the CBSP per instance files.

        The instance name identifies this specific CBSP instance.  It is
        derived from: cbsp_name.  Using this parameters as the file name
        ensures each CBSP instance will have a unique file for recording
        parameters specific to this specific instance.

        @return string with CBSP basename
        """

        if cbsp_name:
            return cbsp_name
        else:
            return ''

    def GetInstanceName(self, options=None, cbsp=False):
        """
        Get either PinPoint or CBSP instance base file name.

        @param options Options given on cmd line

        @return string with base instance name
        """

        if cbsp:
            return self.GetCBSPInstanceName(options)
        else:
            return self.GetPPInstanceName(options)



    def GetInstanceFileName(self, f_ext, options=None, cbsp=False):
        """
        Get PinPoint or CBSP instance file name with the specified file extension.

        @param f_ext File extension
        @param options Options given on cmd line

        @return string with file name
        """

        return self.GetInstanceName(options, cbsp) + instance_ext + f_ext


    #####################################################################################
    #
    # Methods for reading/writing tracing parameters in configuration files.
    #
    #####################################################################################

    def SaveCfgParameter(self, param, value):
        """
        Save a parameter/value pair in a configuration file.

        1) If the parameter is already in the file, then replace with the new
           value.
        2) If the parameter isn't already in the file, then add it.
        3) If the configuration file doesn't exist, then create it.

        @param param Name of parameter to save
        @param value Value of parameter to save

        @return no return
        """


        # Use the 'unique' configuration file for this tracing instance.
        #
        # import pdb ; pdb.set_trace()
        config_file = self.GetInstanceFileName(config_ext)

        # Does the config file already exist?
        #
        if os.path.isfile(config_file):

            # File exist, process it.
            #
            try:
                fp = open(config_file, 'rb')
            except IOError:
                msg.PrintAndExit('SaveCfgParameter(), unable to open per instance config file: ' + \
                    config_file)

            # First, check to see if the section 'Parameters' exists.  If not,
            # then it's not a valid config file.
            #
            # import pdb ; pdb.set_trace()
            string = fp.read()
            fp.seek(0, 0)
            if string.find('Parameters') == -1:

                # Not a valid file, just clean up & return without doing
                # anything.  No need for an error msg, just a warning.
                # 
                # TODO - fix this so it does something reasonable
                #
                msg.PrintMsgPlus('WARNING: Tracing configuration file found, but not valid:\n' + \
                    config_file)
                fp.close()
                return
            else:

                # Open a temporary file. This will be used to create a new
                # config file.  
                #
                # import pdb ; pdb.set_trace()
                tmp_file = tempfile.mkstemp()
                tmp_fp = os.fdopen(tmp_file[0], 'wb')
                tmp_name = tmp_file[1]

                # Backup file name for the current configuration file.
                #
                backup_name = config_file + '.bak'

                # Next, check to see if the parameter already exist in the file.
                #
                if string.find(param) != -1:

                    # Parameter in old file.  Find the line with the parameter.
                    # Generate a line with the param & new value.
                    #
                    new_line = ''
                    for line in fp.readlines():
                        if line.find(param) != -1:
                            new_line = param + ':\t' + value + '\n'

                            # Don't write the current line, it contains the old value.
                            #
                            continue

                        # Write current line to the tmp file.
                        #
                        tmp_fp.write(line)

                    # Now write the parameter/value to the end of tmp file.
                    #
                    if new_line:
                        tmp_fp.write(new_line)
                else:

                    # Parameter not in the old file.  Copy old file to the tmp file.
                    #
                    for line in fp.readlines():
                        tmp_fp.write(line)

                    # Add the new parameter/value to the end of tmp file.
                    #
                    tmp_fp.write(param + ':\t' + value + '\n')

                # Save the old config file (just in case). Then copy the tmp file
                # as the new per instance config file.  Don't use os.rename() to move
                # the file as this may fail if src/dst file systems are different.
                #
                fp.close()
                tmp_fp.close()
                # import pdb ; pdb.set_trace()
                if os.path.isfile(backup_name):
                    os.remove(backup_name)
                if os.path.isfile(config_file):
                    os.rename(config_file, backup_name)
                shutil.copy(tmp_name, config_file)
                os.remove(tmp_name)
        else:

            # Config file does not exist so create it now.
            #
            try:
                fp = open(config_file, 'wb')
            except IOError:
                msg.PrintAndExit('SaveCfgParameter(), unable to open per instance config file: ' + \
                    config_file)
            fp.write('[Parameters]\n')
            fp.write(param + ':\t' + value + '\n')
            fp.close()

        return

    def GetCfgParam(self, param):
        """
        Get the current value of a parameter from the per instance configuration file.

        Always read the file to ensure the value is the latest one.

        @param param Name of parameter to get

        @return value is always returned as a string, since the type of the paramter is unknown
        @return '' if the parameter is not found
        """

        parser = ConfigParser.ConfigParser()
        config_file = self.GetInstanceFileName(config_ext)

        result = ''
        if os.path.isfile(config_file):

            # If the file exists, parse it and get the contents.
            #
            parser.read(config_file)

            # See if the config file has the section 'Parameters'. Then
            # try to read the parameter.  Always read it as string since
            # the type is unknown.
            #
            if parser.has_section(param_section):
                result = self.GetVarStr(param_section, param, parser)

        return result

    def ClearCfgParameter(self, param):
        """
        Clear the value for a parameter in the per instance configuration file.

        Do this by setting the value to '', since the type of the paramter is unknown.

        @param param Name of parameter to clear

        @return no return
        """
        self.SaveCfgParameter(param, '')

"""Initialize the module by reading the default configuration file."""

# import pdb ; pdb.set_trace()
Config = ConfigClass()
params = Config.GetCfgFile(default_config_file, Config.parser)
Config.SetGlobalAttr(params)

