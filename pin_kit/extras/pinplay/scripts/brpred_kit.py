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
# $Id: brpred_kit.py,v 1.29 2015/06/12 22:20:26 tmstall Exp tmstall $

#
# Branch predictor simulator: Contains methods use to generate prediction error.
#

import sys
import os

# Local modules
#
import config
import msg
import kit
import util


class BrPredKit(kit.Kit):
    """
    This class contains methods to run the branch predictor simulator and extract
    the metric of interest from the output files it generates.

    Users can modifying the code in the module to use a different simulator.
    The modules 'config.py' and 'replay_dir.py' must also be modified to
    reflect the new simulator.  All other code should be independent of the
    simulator.

    Alternatively, the code in this module can be used as a template to add a new simulator,
    in addition to the branch predictor simulator. To do this, all files which start with
    the string 'brpred_' must be cloned/modified for the new simulator.

    The following instructions are based on modifying the existing simulator
    (first case listed above).  If adding a new simulator, the additional modifications
    to the other 'brpred_' files, beyond the instructions given below, should be self-evident.

    I. It is assumed the simulator has at least these two options:

        1) Specify name of the file for output data
                Knob for branch predictor is '-statfile file'

        2) Print simulator data for the metric of interest every 'icount'
           instructions.  The code that runs the simulator uses this option
           with the tracing parameter 'warmup_length'.  See method GetRegionMetric()
           for more information.
                Knob for branch predictor is '-phaselen icount'

    II. Steps required in this module to add a new simulator:

        1) Modify the attributes in this class to reflect the requirements of
           the new simulator (in section "simulator control knobs").  If the only
           changes required to create a simulator kit are in this module, then the
           remaining changes are described in section III.

        2) The base class for this class is in the module 'kit.py'.  If you
           need to modify values in the base class, then there may be other
           locations in the scripts which may also need to be modified.

        3) Modify the following method to reflect the knobs in the new simulator:
                GetSimOutputFile()

        4) Modify method RunSimulator() to run the new simulator.

        5) Modify these two methods to extract the metric of interest from the
           new simulator data files:
               GetLastMetric()
               GetRegionMetric()

    III. Steps required in other modules to add a new simulator:

        1) Add a type for the new simulator in the module 'config.py' under "Types of kits".

        2) Modify the method Replay() in 'replay_dir.py'.  Search for the
           string 'options.sim_add_filename' and add code to instantiate a kit for
           the new simulator type.
    """

    # What type of a kit is this.
    #
    kit_type = config.BRPRED

    pintool = 'pinplay-branch-predictor.so'

    ###################################################################
    #
    # Define simulator control knobs & file extension
    #
    ###################################################################

    # Default branch predictor knobs. 
    #
    default_knobs = ''

    # File extension for output file.
    #
    file_ext = '.brpred.txt'

    # Define the knob that generates an output file.
    #
    output_knob = '-statfile'

    # Define the knob that dumps data after executing
    # a given number of instructions.
    #
    print_data_knob = '-phaselen'

    ###################################################################
    #
    # Methods used to run the Branch Predictor simulator and get information
    # from output files required to calculate prediction error.
    #
    ###################################################################

    def GetSimOutputFile(self, basename):
        """
        Get the knob & base file name required to generate an output file for the simulator.

        @param basename Basename (file name w/o extension) of pinball to process

        @return string containing the kob
        """

        return self.output_knob + ' ' + basename + self.file_ext

    def RunSimulator(self, dirname, sim_replay_cmd, phase_length, options):
        """
        Run Branch Predictor simulator on all the pinballs in a set of
        directories given by 'dirname'.

        @param dirname Directory containing pinball to process
        @param sim_replay_cmd Script used to replay pinball
        @param phase_length Number of instructions executed before CMPSim will print next set of output
        @param options Options given on cmd line

        @return error code from running the simulator
        """

        # Don't do anything if running in debug mode.
        #
        if options.debug:
            return 0

        gv = config.GlobalVar()

        # Need to let script 'sim_replay_cmd' know type of kit is calling it.
        #
        config.sim_kit_type = self.kit_type

        # Run on all the pinballs in 'dirname'.
        #
        cmd = sim_replay_cmd + ' --replay_dir ' + dirname
        cmd += ' --log_options'

        cmd += ' "'
        # 'phase_length' is given in instructions.
        #
        cmd += ' ' + self.print_data_knob + ' ' + str(phase_length)
        cmd += '" '

        cmd += ' --sim_add_filename'  # This causes simulator output file name to be added to cmd.
        cmd += util.AddCfgFile(options)
        cmd += util.AddGlobalFile(gv.DumpGlobalVars(), options)

        end_str = ''  # Don't want to print anything when this cmd finishes.
        # import pdb;  pdb.set_trace()
        result = util.RunCmd(cmd, options, end_str, concurrent=False)

        return result

    def GetLastMetric(self, sim_file, tid, options):
        """
        Get the last metric in a simulator output file.  This is the value
        for running the entire pinball.

        @param sim_file File with simulator results to process
        @param options TID of results to be processed
        @param options Options given on cmd line

        @return metric
        """

        # Get a file pointer to the simulator data file.
        #
        f = util.OpenCompressFile(sim_file)
        if f == None:
            # Error opening file, return an error.
            #
            return -1.0

        # This is the code which needs to be modified in order to use a
        # different metric of interest for a new simulator.  The existing code
        # uses the metric MPI (misses per thousand instruction).  
        #
        # Current code assume the default Branch Predictor simulator is used. Get the
        # number of instructions and misses for this thread in the last line of
        # the output.
        #
        # import pdb ; pdb.set_trace()
        instr = misses = 0
        for line in f.readlines():
            pos = line.find('Icount: ')
            if pos != -1:
                last = line
        # import pdb ; pdb.set_trace()
        lst = last.split()
        instr = int(lst[1])
        misses = int(lst[3])

        # Check to make sure there really is valid data.  If not, the print a
        # warning.  No need to exit with an error, because it's possible for
        # MPI_MT_MODE applications to have a different number of threads in
        # each process.  This means some processes may have a thread 'tid',
        # while this process may not.
        #
        if instr > 1:
            metric = misses / (float(instr) / 1000)
        else:
            msg.PrintMsgPlus('WARNING: There were no instructions in simulator output for thread ' + \
                                   str(tid) + ' in file:\n         ' + sim_file)
            msg.PrintMsg('Prediction error for this process may be suspect.')
            if options.mode == config.MPI_MT_MODE:
                msg.PrintMsg('Since tracing mode is \'mpi_mt\', this may be OK.')
            metric = -1.0  # Error indication

        return metric

    def GetRegionMetric(self, sim_file, warmup, tid, options):
        """
        Get the metric of interest for just the representative region, not including
        any warmup instructions.

        It is assumed the first set of simulator output data is for the warmup
        instructions, if they exist.  This is true because when the simulator was run
        the knob to print out data was called with 'warmup_length' instructions.

        The last set of data will be for both the representative region and
        warmup instructions, if any exist.

        Of course, if there's only one set of data, then it is for the region only,
        because there aren't any warmup instruction.

        @param sim_file File with simulator results to process
        @param warmup Number of instructions in warmup section
        @param options TID of results to be processed
        @param options Options given on cmd line

        @return metric
        """

        # Get a file pointer to the simulator data file.
        #
        f = util.OpenCompressFile(sim_file)
        if f == None:
            # Error opening file, return an error.
            #
            return -1.0

        # This is the code which needs to be modified in order to use a
        # different metric of interest for a new simulator.  The existing code
        # uses the metric MPI (misses per thousand instruction).  
        #
        # Current code assume the default Branch Predictor simulator is used. 
        #
        # Get the first and last lines in the output that have the
        # cycle/instruction counts.  Assume the 1st is always the info for the
        # warmup because the simulator data is dumped ever 'warmup_length'
        # instructions.  Assume last data point is for warmup + region.  If
        # there is only one line, then assume it's only for the region.
        #
        # Always use the data for thread 0 because we don't generate functional
        # correlation for cooperative region pinballs.  Need to fix this when
        # this capability is added.
        #
        # import pdb ; pdb.set_trace()
        first = ''
        last = ''
        for line in f.readlines():
            pos = line.find('Icount:')
            if pos != -1:

                # If the first time, save it.
                #
                if first == '':
                    first = line
                last = line
        # import pdb ; pdb.set_trace()
        l_list = last.split()
        l_instr = int(l_list[1])
        l_misses = int(l_list[3])

        if warmup == 0:
            # No warmup. Calc metric from the last set of data.
            #
            if l_instr > 0:
                metric = l_misses / (float(l_instr) / 1000)
            else:
                msg.PrintAndExit('(1) Unable to calculate metric because number of instructions is 0:\n' \
                    '            ' + sim_file)
        else:
            # Get number of instructions & misses for first set of data. (from warmup)
            #
            f_list = first.split()
            f_instr = int(f_list[1])
            f_misses = int(f_list[3])

            # Calculate region data by subtracting the last values from the
            # first values. This gives number of misses and instructions for
            # just the region.
            #
            # Check to make sure there really is valid data.  If not, the print a
            # warning.  No need to exit with an error, because it's possible for
            # MPI_MT_MODE applications to have a different number of threads in
            # each process.  This means some processes may have a thread 'tid',
            # while this process may not.
            #
            # import pdb ; pdb.set_trace()
            if l_instr - f_instr > 0:
                metric = (l_misses - f_misses) / (float(l_instr - f_instr) / 1000)
            else:
                msg.PrintMsgPlus('WARNING: There were no instructions in simulation output for thread ' + \
                                       str(tid) + ' in file:\n         ' + sim_file)
                msg.PrintMsg('Prediction error for this process may be suspect.')
                if hasattr(options,
                           'mode') and options.mode == config.MPI_MT_MODE:
                    msg.PrintMsg(
                        'Since tracing mode is \'mpi_mt\', this may be OK.')
                metric = -1.0

        return metric
