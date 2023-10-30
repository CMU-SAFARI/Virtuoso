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
# $Id: sde_cmpsim_kit.py,v 1.48 2015/06/12 22:20:26 tmstall Exp tmstall $

#
# Branch predictor simulator: Contains methods use to generate prediction error.
#

import sys
import os
import locale
import glob
locale.setlocale(locale.LC_ALL, "")

# Local modules
#
import config
import msg
import sde_kit
import util


class CMPsimKit(sde_kit.SDEKit):
    """
    This class contains methods to run CMPSim and analyze
    the output files it generates.
    """

    # What type of a kit is this.
    #
    kit_type = config.CMPSIM

    pintool = 'sde-cmpsim.so'

    ###################################################################
    #
    # Define simulator control knobs & file extension
    #
    ###################################################################

    # Default CMPSim knobs. This sets up L1, L2, & L3 caches and generates data for
    # use in calculating function correlation.
    #
    # Full sized LLC
    #
    # -FLC 32 -MLC 256 -LLC 4096
    #
    # This definition uses 1/2 the "normal" size LLC.
    #
    # -FLC 32 -MLC 256 -LLC 2048
    #
    # Generate LCAT trace files which only contain memory references for L2 cache misses.
    #
    # -FLC 32 -MLC 256 -hier 1,1 -gentrace 1
    #
    # Run CMPSim in 'explore' mode
    #
    # -threads 1 -hier 1 -repl 0 -unified 1 -phaselen 100 DL1:131072:64:8192:1
    #
    default_knobs = '-FLC 32 -MLC 256 -LLC 4096'

    # File extension for output file.
    #
    file_ext = '.CMPSim.txt'

    # Define the knob that generates an output file.
    #
    output_knob = '-o'

    # Define the knob that dumps data after executing
    # a given number of instructions.
    #
    print_data_knob = '-phaselen'

    # SDE kits don't require an explicit knob to define the tool.  At one time,
    # the PinPlay tools didn't add a pintool to the command line. Thus, this method
    # returns the empty string.
    #
    # However, on 12/8/14 we returned to explicitly defining a knob for the pintool.
    # This was done in order to allow the user to define the pintool using the option
    # '--pintool'.  At that time, this method was commented out.
    #
    # This code is kept so that in the future if the knob is not used any more, just
    # re-enable this code.
    #
    #    def GetPinToolKnob(self, pinball=''):
    #        """
    #        Get the knob required to add the pintool for this kit to the Pin command line.
    #        Some kits don't required a pintool knob.  In this case, just return an empty string.
    #
    #        SDE CMPsim based kits require a pintool knob, so return it.
    #        """
    #
    #        return ' -t ' + self.GetPinTool()

    ###################################################################
    #
    # Methods used to run CMPSim and get information from CMPSim output files
    # required to calculate prediction error.
    #
    ###################################################################

    def GetSimOutputFile(self, basename):
        """
        Get the knob & base file name required to generate an output file for the simulator.

        @param basename Basename (file name w/o extension) of pinball to process

        @return String containing knob & pinball
        """

        return self.output_knob + ' ' + basename + self.file_ext

    def RunSimulator(self, dirname, sim_replay_cmd, phase_length, options):
        """
        Run a simulator on all the pinballs in a directory given by 'dirname'.

        @param dirname Directory containing pinball to process
        @param sim_replay_cmd Script used to replay pinball
        @param phase_length Number of instructions executed before CMPSim will print next set of output
        @param options Options given on cmd line

        @return Error code from running the simulator
        """

        # Check to make sure the directory at least exists.
        #
        # import pdb;  pdb.set_trace()
        if not os.path.exists(dirname):
            if options.mode == config.MPI_MT_MODE:
                msg.PrintMsg('WARNING: Directory containing pinballs to run with simulator does not exist:\n   ' + \
                    dirname)
                msg.PrintMsg('Since tracing mode is \'mpi_mt\', this may be OK.')
                return 0
            else:
                msg.PrintMsg('ERROR: Directory containing pinballs to run with simulator does not exist:\n   ' + \
                    dirname)
                return -1

        # Don't do anything if running in debug mode.
        #
        if options.debug:
            return 0

        gv = config.GlobalVar()

        # Get the number of threads in the pinball and round it up
        # to the next power of 2.  This is a requirement of CMPSim.
        #
        # import pdb;  pdb.set_trace()
        threads = util.GetNumThreadsWP(options)
        threads = util.RoundupPow2(threads)

        # Need to let script 'sim_replay_cmd' know type of kit is calling it.
        #
        config.sim_kit_type = self.kit_type

        # Run on all the pinballs in 'dirname'.
        #
        cmd = sim_replay_cmd + ' --replay_dir ' + dirname
        cmd += ' --replay_options'
        cmd += ' "' + options.replay_options + '"'
        cmd += ' --log_options'
        cmd += ' "'
        cmd += '-threads ' + str(threads)

        # If the directory contains the WP directory name for this tracing
        # instance, then get the icount for each process/thread in the WP
        # pinballs.  Use the longest icount as the phase_length for CMPSim.
        #
        # This is done to reduce the CMPSim output to the very minimum for WP
        # pinballs.  For very large WP pinballs, using the 'default' value for
        # phase_length generates CMPSim output which can be multiple GBs in size.
        #
        # import pdb;  pdb.set_trace()
        if util.GetWPDir() in dirname:
            max_icount = 0
            pinballs = glob.glob(os.path.join(dirname, '*.address'))
            for pb in pinballs:
                fname = os.path.basename(pb)
                icount = util.GetMaxIcount(dirname, fname)
                if icount > max_icount:
                    max_icount = icount
            # Check for errors
            #
            # import pdb;  pdb.set_trace()
            if max_icount != 0:
                phase_length = max_icount / 1000000
                if phase_length == 0:
                    # Must have a length of at least 1
                    #
                    phase_length = 1

        cmd += ' ' + self.print_data_knob + ' ' + str(phase_length)
        cmd += '" '
        cmd += ' --sim_add_filename'  # This causes CMPSim output file name to be added to cmd
        cmd += util.AddCfgFile(options)
        cmd += util.AddGlobalFile(gv.DumpGlobalVars(), options)

        # import pdb;  pdb.set_trace()
        end_str = ''  # Don't want to print anything when this cmd finishes.
        result = util.RunCmd(cmd, options, end_str, concurrent=False)

        return result

    def GetLastMetric(self, sim_file, tid, options):
        """
        Get the last metric in a CMPSim output file.  This is the value
        for running the entire pinball.

        Seek until we are close to the end of the file before start looking
        for data. This saves lots of time when processing very large files.

        @param sim_file File with simulator results to process
        @param options TID of results to be processed
        @param options Options given on cmd line

        @return metric (-1 if an error occurs)
        """

        import struct

        # Get the size of the uncompressed simulator data file from the last 4 bytes
        # of the compressed file.  This value is the file size modulo 2^32.
        #
        # import pdb ; pdb.set_trace()
        try:
            fo = open(sim_file, 'rb')
        except IOError:
            msg.PrintMsg('ERROR: Unable to open CMPSim file for whole program pinball:\n   ' + \
                sim_file)
            return -1.0
        try:
            fo.seek(-4, 2)
        except:
            msg.PrintMsg('ERROR: There was a problem accessing data for the WP CMPSim file:\n   ' + \
                sim_file)
            return -1.0
        r = fo.read()
        fo.close()
        size = struct.unpack('<I', r)[0]

        # Get a file pointer to the simulator file.
        #
        f = util.OpenCompressFile(sim_file)
        if f == None:
            return -1.0

        four_GB = 4294967296
        seek_past = 100
        num_chunk = 0

        # First seek to the point in the file given by the 'size'.
        #
        msg.PrintMsgPlus('Determining size of file: ' + sim_file)
        f.seek(size, 1)
        current = f.tell()

        # For files > 4GB, the value for 'size' is the true file size modulo
        # 2^32.  If this is the case, seek in 4GB chunks until the true file
        # size is found.
        #
        # import pdb ; pdb.set_trace()
        while current - (num_chunk * four_GB) >= size:

            # First see if we can seek a few bytes past the current file
            # pointer location.  If we don't advance the FP, then it's at the
            # end of the file. Otherwise, there is a 4GB chunk of the file to
            # be bypassed.
            #
            # import pdb ; pdb.set_trace()
            last = current
            f.seek(seek_past, 1)
            current = f.tell()
            if current == last:
                break
            else:
                msg.PrintMsg('Skipping 4GB in CMPSim file')
                f.seek(four_GB - seek_past, 1)
                num_chunk += 1
                current = f.tell()

            # Check to see if the last seek reached 'size' modulo 2^32
            # bytes. If so, then we are at the end of the file.
            #
            if current - (num_chunk * four_GB) < size:
                break

        # import pdb ; pdb.set_trace()
        size = num_chunk * four_GB + size

        # Skip to 100k bytes before the end of the file. Then start looking for the last set of
        # data in the file. This saves a large amount of time, especially for huge files.
        #
        msg.PrintMsgPlus('Skipping ' + locale.format('%d', size, True) +
                         ' bytes in file: ' + sim_file)
        f.seek(0)
        f.seek(size - 100000)

        # This is the code which needs to be modified in order to use a
        # different metric of interest for a new CMPSim.  The existing code
        # uses the metric CPI.
        #
        # Current code assume the is used. Get the
        # number of instructions and cycles for this thread in the last line of
        # the output.
        #
        instr = cycles = 0
        for line in f.readlines():
            pos = line.find('Thread: ' + str(tid) + ' Instructions:')
            if pos != -1:
                last = line
        # import pdb ; pdb.set_trace()
        lst = last.split()
        instr = int(lst[3])
        cycles = int(lst[5])

        # Check to make sure there really is valid data.  If not, the print a
        # warning.  No need to exit with an error, because it's possible for
        # MPI_MT_MODE applications to have a different number of threads in
        # each process.  This means some processes may have a thread 'tid',
        # while this process may not.
        #
        if instr > 1:
            metric = cycles / float(instr)
        else:
            msg.PrintMsgPlus('WARNING: There were no instructions in WP CMPSim output for thread ' + \
                                   str(tid) + ' in file:\n         ' + sim_file)
            msg.PrintMsg(
                'Prediction error will not be calculated for this process.')
            if options.mode == config.MPI_MT_MODE:
                msg.PrintMsg('Since tracing mode is \'mpi_mt\', this may be OK.')
            metric = -1.0

        return metric

    def GetRegionMetric(self, sim_file, warmup, tid, options):
        """
        Get the metric of interest for just the representative region, not including
        any warmup instructions.

        It is assumed the first set of CMPSim output data is for the warmup
        instructions, if they exist.  This is true because when the CMPSim was run
        it should have printed out data at 'warmup_len' intervals.

        The last set of data will be for both the representative region and
        warmup instructions, if any.

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
            return -1.0

        # This is the code which needs to be modified in order to use a
        # different metric of interest for a new CMPSim.  The existing code
        # uses the metric CPI.
        #
        # Get the first and last lines in the output that have the
        # cycle/instruction counts.  Assume the 1st is always the info for the
        # warmup because the CMPSim data is dumped ever 'warmup_length'
        # instructions.  Assume last data point is for warmup + region.  If
        # there is only one line, then assume it's only for the region.
        #
        # Current code assume the default Branch Predictor CMPSim is used. 
        #
        # Always use the data for thread 0 because we don't generate prediction
        # error for cooperative region pinballs.  Need to fix this when
        # this capability is added.
        #
        # import pdb ; pdb.set_trace()
        first = ''
        last = ''
        for line in f.readlines():
            pos = line.find('Thread: ' + str(0) + ' Instructions:')
            if pos != -1:

                # If the first time, save it.
                #
                if first == '':
                    first = line
                last = line
        # import pdb ; pdb.set_trace()
        l_list = last.split()
        l_instr = int(l_list[3])
        l_cycles = int(l_list[5])

        if warmup == 0:
            # No warmup. Calc metric from the last set of data.
            #
            if l_instr > 0:
                metric = l_cycles / float(l_instr)
            else:
                msg.PrintAndExit('(1) Unable to calculate CPI because number of instructions is 0:\n' \
                    '            ' + sim_file)
        else:
            # Get number of instructions & cycles for first set of data. (from warmup)
            #
            f_list = first.split()
            f_instr = int(f_list[3])
            f_cycles = int(f_list[5])

            # Calculate region data by subtracting the last values from the
            # first values. This gives number of cycles and instructions for
            # just the region.
            #
            # Check to make sure there really is valid data.  If not, the print a
            # warning.  No need to exit with an error, because it's possible for
            # MPI_MT_MODE applications to have a different number of threads in
            # each process.  This means some processes may have a thread 'tid',
            # while this process may not.
            #
            if l_instr - f_instr > 0:
                metric = (l_cycles - f_cycles) / float(l_instr - f_instr)
            else:
                # import pdb ; pdb.set_trace()
                msg.PrintMsgPlus('WARNING: It looks like there were no warmup instructions in region CMPSim output for thread ' + \
                   str(tid) + ' in file:\n         ' + sim_file)
                msg.PrintMsg('First icount: %s    Last icount: %s' % (locale.format('%d', f_instr, True), \
                    locale.format('%d', l_instr, True)))
                if l_instr < config.instr_cmpsim_phase:
                    msg.PrintMsg(
                        'Slice size may be too small to calculate prediction error.')
                    msg.PrintMsg(
                        'It needs to be at least 1,000,000 for CMPSim to generate valid data.')
                msg.PrintMsg('Prediction error for this process may be suspect.')
                if hasattr(options,
                           'mode') and options.mode == config.MPI_MT_MODE:
                    msg.PrintMsg(
                        'Since tracing mode is \'mpi_mt\', this may be OK.')
                metric = -1.0

        return metric
