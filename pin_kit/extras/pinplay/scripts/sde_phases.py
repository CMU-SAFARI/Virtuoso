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
# $Id: sde_phases.py,v 1.65 2015/12/09 22:11:31 tmstall Exp tmstall $

# Print out messages, including error messages
#
#

import sys
import os
import shutil
import subprocess
import glob

# Local modules
#
import config
import msg
import util
import phases


class SDEPhases(phases.Phases):
    """
    This module contains the tracing phases used in high level scripts which are only
    run with SDE.  Phases common to both PinPlay and SDE are in the module 'phases.py'.

    In order to use these phases, almost all of them require an options object
    and a config object.
    """

    traceinfo_bin = 'traceinfo.py'

    ###################################################################
    #
    # Methods to generate the traceinfo files. 
    #
    ###################################################################

    def GenTraceinfoFiles(self, options):
        """
        Generate traceinfo files from sysinfo files. Also do some simple error
        checking.

        One traceinfo file is generated for each directory which contains
        region pinballs.  Currently, the script 'create_traceinfo.sh' is used
        to generate the traceinfo file.  This will be replaced with a Python
        script at sometime in the future.

        The following info is printed to allow the user to determine if the
        required number of region pinballs and traces were generated:
        1) The script 'count_traces.sh' gets a count of the number of
           expected traces and the region numbers from the region CSV file.
        2) The number of actual traces generated and the trace names.
        3) The number of region pinballs generated and the pinball names.
        """

        # Check to make sure there is at least one region pinball directory for
        # this tracing instance.
        #
        all_pp_dirs = util.GetRegionPinballDir(options)
        if all_pp_dirs == [] and not options.debug:
            msg.PrintMsg(
                '\nERROR: Could not find any PinPoint \'*.pp\' directories.\n'
                'Make sure you have run the phase \'-p\'.')
            return -1

        # Get the region pinball directories for this tracing instance.
        #
        # import pdb;  pdb.set_trace()
        all_lit_dirs = util.GetLitDir()
        if all_lit_dirs == [] and not options.debug:
            msg.PrintMsg(
                '\nERROR: Could not find any LIT \'*.lit\' directories.\n'
                'Make sure you have run the phase \'-L\'.')
            return -1

        result = 0
        # import pdb;  pdb.set_trace()
        for lit_dir in all_lit_dirs:

            # Get the name of the corresponding region pinball directory
            # and make sure it exists.
            #
            pp_dir = util.ChangeExtension(lit_dir, '.lit', '.pp')
            if not os.path.isdir(pp_dir):
                if not options.list:
                    msg.PrintMsgPlus('WARNING: Generating traceinfo files, but the required \'pp\' ' \
                        'directory does not exist:\n   ' + pp_dir)

                    # If running in MPI_MT_MODE, then it's possible for one process to
                    # not have a thread corresponding to the the current focus thread.
                    # However, another process might have this thread.
                    # Thus, only return an error if not tracing a MPI_MT application.
                    #
                    if options.mode == config.MPI_MT_MODE:
                        msg.PrintMsg(
                            'Since tracing mode is \'mpi_mt\', this may be OK.')
                        continue
                    else:
                        return -1
                else:
                    # Not printing any msgs, just skip to the next directory
                    #
                    continue

            # Make sure the LIT directory exists, then go there.
            #
            old_dir = os.getcwd()
            if os.path.isdir(lit_dir):
                os.chdir(lit_dir)
            else:
                if not options.list:
                    msg.PrintMsgPlus('WARNING: Generating traceinfo files, but the LIT ' \
                        'directory does not exist:\n   ' + lit_dir)

                    # If running in MPI_MT_MODE, then it's possible for one process to
                    # not have a thread corresponding to the the current focus thread.
                    # However, another process might have this thread.
                    # Thus, only return an error if not tracing a MPI_MT application.
                    #
                    if options.mode == config.MPI_MT_MODE:
                        msg.PrintMsg(
                            'Since tracing mode is \'mpi_mt\', this may be OK.')
                        continue
                    else:
                        return -1
                else:
                    # Not printing any msgs, just skip to the next directory
                    #
                    continue

            # Copy the traceinfo 'blank' XML files from the SDE kit.
            #
            blank_path = self.kit_obj.GetTraceinfoBlank()
            for blank in config.traceinfo_blank_files:
                blank_file = os.path.join(blank_path, blank)
                try:
                    shutil.copy(blank_file, os.getcwd())
                except IOError:
                    msg.PrintMsg(
                        '\nERROR: Unable to copy traceinfo \'blank\' file:\n    '
                        + blank_file)
                    return -1

            # Run the script to generate traceinfo XML file.  Stdout from the
            # script is the XML file.  Function util.RunCMD() needs the output XML
            # file object in order to write this file.
            #
            msg.PrintMsg('')
            base_name = util.ChangeExtension(lit_dir, '.lit', '')
            tr_file = base_name + '.traceinfo.xml'
            try:
                fp_out = open(tr_file, 'w')
            except IOError:
                msg.PrintMsg('ERROR: Failed to open traceinfo output file:\n'
                             '   ' + tr_file)
                return -1
            cmd = self.traceinfo_bin + ' ' + base_name
            result = util.RunCmd(cmd, options, '',
                                 concurrent=False,
                                 f_stdout=fp_out)
            if result != 0:
                msg.PrintMsg('Error found while running script \'%s\'' %
                             self.traceinfo_bin)
                return -1

            # Print info from the CSV regions file (located in the *.Data
            # directory) about the number of expected traces.
            #
            msg.PrintMsg('')
            param = {'in_lit_dir': True}
            cluster_info, not_used, total_instr = util.GetClusterInfo(base_name, param)
            if cluster_info == {}:
                msg.PrintMsg(
                    'ERROR: Problems getting cluster info from file: %s.pinpoints.csv'
                    % (base_name))
                return -1
            cluster_list = util.ParseClusterInfo(cluster_info)
            if len(cluster_info) != len(cluster_list):
                msg.PrintMsg('ERROR: Did not parse enough clusters from CSV file: %s.pinpoints.csv\n' '   Num clusters:         %d\n' \
                    '   Num parsed clusters:  %d' % (base_name, len(cluster_info), len(cluster_list)))
                return -1

            # Print the number of expected traces from the regions CSV file.
            #
            base_tid = -1
            for cl in cluster_list:
                if (cl.has_key('tid')):
                    tid = cl['tid']
                else:
                    msg.PrintMsg(
                        'ERROR: Parsing cluster info for cluster:\n     %s' %
                        (cl))
                    return -1
                if base_tid == -1:
                    base_tid = tid
                else:
                    if tid != base_tid:
                        msg.PrintAndExit(
                            'ERROR: Expected TID %d, but found TID %d' %
                            (base_tid, tid))
                        return -1
            msg.PrintMsg('Expected trace count: %d\n' % (len(cluster_info)))

            # Print the number of actual traces in the LIT directory and the names. 
            #
            # import pdb;  pdb.set_trace()
            if not options.list:
                msg.PrintMsg(
                    'Actual trace count: ' + str(util.CountFiles('ami')))
                lit_files = glob.glob('*.ami*')
                lit_files.sort()
                for f in lit_files:
                    msg.PrintMsg('   ' + f)

            # Clean up tmp files in the LIT directory.
            #
            tmp_files = glob.glob('*blank*.xml')
            for f in tmp_files:
                os.remove(f)

            # Return to the working directory.
            #
            os.chdir(old_dir)

            # Go to the *.pp directory. Print the number of actual pinballs and the names.
            #
            os.chdir(pp_dir)
            if not options.list:
                msg.PrintMsg(
                    '\nPinball count: ' + str(util.CountFiles('address')))
                pp_files = glob.glob('*.address')
                pp_files.sort()
                for f in pp_files:
                    msg.PrintMsg('   ' + f)

                # Print a warning if the expected number of traces are not found.
                #
                if len(lit_files) != len(pp_files):
                    msg.PrintMsgPlus(
                        'WARNING: Number of traces does not match the number of region pinballs.')

            msg.PrintMsg('\nGenerated traceinfo file: ' + tr_file)

            # Return to the working directory.
            #
            os.chdir(old_dir)

        return result

    ###################################################################
    #
    # Methods to generate LIT files.
    #
    ###################################################################

    # Values used to determine which flavor of files to generate.
    #
    LMAT = 0
    LIT = 1

    def GenLitFiles(self, param, dirname, file_name):
        """
        Generate either the LIT files or warmup LMAT files for one pinball based on the
        value 'flavor' passed in the param dictionary.

        The region pinball contains optional warmup instructions before the instructions
        in the actual representative regions.  If there aren't any warmup instructions for a
        pinball, then skip the step which generates LMAT files.
        """

        # Setup locale so we can print integers with commas.
        #
        import locale
        locale.setlocale(locale.LC_ALL, "")

        # Define a lambda function to print an error message.
        #
        err_msg = lambda string: msg.PrintMsg(
            'ERROR: method sde_phases.GenLitFiles() failed to '
            'get field: ' + string)

        # Unpack replay_cmd, options and flavor from the dictionary param.
        # Flavor determines the type of file to generate.
        #
        if param.has_key('flavor'):
            flavor = param['flavor']
        else:
            err_msg('flavor')
            return -1
        if param.has_key('options'):
            options = param['options']
        else:
            err_msg('options')
            return -1
        if param.has_key('replay_cmd'):
            replay_cmd = param['replay_cmd']
        else:
            err_msg('replay_cmd')
            return -1

        result = 0

        # Get information from the pinball and the pinball file name.
        # Dummy variables: a, b, c, d, e, f
        #
        (icount, warmup, prolog, calc_region, epilog, tid,
         region) = util.GetRegionInfo(os.path.join(dirname, file_name), options)
        (a, b, c, file_region, d, e, f) = util.GetRegionInfo(
            os.path.join(dirname, file_name), options,
            use_file_fields=True)

        # If generating LMAT files, yet there aren't any warmup instructions for this pinball, then skip
        # this file by returning from the method.
        #
        if not options.list:
            if flavor == self.LMAT and warmup == 0:
                msg.PrintMsgPlus(
                    "NOTE: No warmup instructions.  Not generating LMAT files for trace:\n"
                    "       " + file_name)
                return 0

        # Check to make sure the TID in file name is the same as the tracing parameter 'focus_thread'.
        #
        if tid != config.focus_thread and config.focus_thread >= 0:
            msg.PrintMsg('ERROR: When generating LIT files, the TID in a pinball\n' \
                    'file name is not the same as the tracing parameter \'focus_thread\'. \n' \
                    'Focus thread is: ' + str(config.focus_thread) + ', yet the TID ' \
                    'in the file name is: ' + str(tid) + '.\n' \
                    'Please double check to make sure the focus thread parameter is \n' \
                    'correct (given with \'option -f X\' where X is the TID.)\n' \
                    '   ' + file_name + '\n')
            return -1

        # Calculate The length of all regions except the warmup
        #
        lit_len = icount - warmup

        # Get the number of threads from the result files
        #
        field = util.FindResultString(
            os.path.join(dirname, file_name + '.result'), 'static_threads')
        nthreads = field[0]
        if not nthreads:
            # Perhaps we are dealing with a pinball version > 2.4.  Look in the
            # *.global.log file for the number of threads.
            #
            field = util.FindString(
                os.path.join(dirname, file_name + '.global.log'), 'static_threads')
            nthreads = field[0]
        if nthreads:
            nthreads = int(nthreads)
        else:
            nthreads = 0

        # Check to make sure there are a significant number of instructions in the pinball.
        #
        if calc_region < 1000:
            msg.PrintMsg(
                '=============================================================')
            msg.PrintMsg(
                'WARNING: The icount for focus thread region pinball is low:')
            msg.PrintMsg('           Dir:               ' + dirname)
            msg.PrintMsg('           File:              ' + file_name)
        else:
            if not options.list:
                msg.PrintMsgPlus('File: ' + file_name)

        if not options.list:
            # Print details about the various sections in the pinball.
            #
            msg.PrintMsg('           Warmup count:        ' +
                         locale.format('%14d', warmup, True))
            msg.PrintMsg('           Prolog count:        ' +
                         locale.format('%14d', prolog, True))
            msg.PrintMsg('           Actual region count: ' + locale.format('%14d', calc_region, True) + \
                '   (from file name: ' + locale.format('%d', file_region, True) + ')')
            msg.PrintMsg('           Epilog count:        ' +
                         locale.format('%14d', epilog, True))

            msg.PrintMsg('           Total Instr count:   ' +
                         locale.format('%14d', icount, True))
            msg.PrintMsg('           Number of threads:   ' +
                         locale.format('%14d', nthreads, True))
            if config.focus_thread >= 0:
                msg.PrintMsg('           Focus thread:                     ' +
                             str(config.focus_thread))

        # Get the 'base' file name without the TID.
        #
        base_name = util.RemoveTID(file_name)

        # Get the LIT directory name from the region pinball directory name.
        #
        lit_dir = util.ChangeExtension(dirname, '.pp', '.lit')

        # Several file names, with path, for pinball and LIT files.  
        #
        # Need to remove TID for the file name given to knob '-log:basename'
        # when generating LMAT files.  This must be done is because pinLIT will
        # add the TID to the LIT file names. Hence it must be removed in the
        # 'basename'.
        #
        # import pdb;  pdb.set_trace()
        pb_path_file = os.path.join(dirname, file_name)
        lit_path_file = os.path.join(lit_dir, file_name)
        lit_path_file_no_tid = os.path.join(lit_dir, base_name)

        # msg.PrintMsg('pb_path_file:         ' + pb_path_file)
        # msg.PrintMsg('lit_path_file:        ' + lit_path_file)
        # msg.PrintMsg('lit_path_file_no_tid: ' + lit_path_file_no_tid)

        # Path & file name to the PTOV file which is generated by pinLIT.  If
        # there are no warmups for the file, then there won't be a 'warmup.ptov'
        # file for it.
        #
        #
        ptov_path_file = glob.glob(
            os.path.join(lit_dir, base_name) + '*.warmup.ptov')
        if ptov_path_file:
            ptov_path_file = ptov_path_file[0]

        # Format the initial part of the command line which is used for both LMAT and LIT files.
        #
        cmd_0 = replay_cmd
        cmd_0 += ' --replay_file ' + pb_path_file
        cmd_0 += ' --log_options '

        # Determine if all parameters should be dumped to the global file
        # (PinPlay), or just a subset (CBSP).
        #
        if options.cbsp():
            pinplay = False
        else:
            pinplay = True
        cmd_1 = util.AddGlobalFile(self.gv.DumpGlobalVars(pinplay), options)
        cmd_1 += util.AddCfgFile(options)

        # Add user defined 'lit_options' to both the LMAT and LIT command
        # lines.  May want sepatate options in the future, but right now
        # there's only one.
        #
        knobs = ''
        if hasattr(
            options,
            'replay_options') and options.replay_options:  # Add any user defined replay options
            knobs += ' ' + options.replay_options
        if hasattr(
            options,
            'lit_options') and options.lit_options:  # Add any user defined replay options
            knobs += ' ' + options.lit_options

        if flavor == self.LMAT:
            # Format the knobs required to generate LMAT files.
            #
            knobs += ' -log -xyzzy -log:LIT -log:LIT_warmup ' + self.kit_obj.knob_length + ' ' + str(
                warmup)
            knobs += ' -log:early_out'
            knobs += ' -log:basename ' + lit_path_file_no_tid
            if hasattr(options, 'compressed'):
                knobs += ' -log:compressed ' + options.compressed
            knobs += util.GetMsgFileOption(lit_path_file + '.lmat')

            if hasattr(options, 'list') and not options.list:
                msg.PrintMsgPlus('Generating LMAT files: ' + file_name)
            end_str = file_name

        else:
            if warmup > 0 and ptov_path_file:
                # Need this knob when generating LIT files if LMAT files already generated
                # for this trace.
                #
                knobs += ' -log:LIT_use_ptov ' + ptov_path_file

            # Format the knobs required to generate LIT files.
            #
            #import pdb;  pdb.set_trace()
            knobs += ' -log -log:LIT ' + self.kit_obj.knob_skip + ' ' + str(warmup)
            if lit_len < 0:
                msg.PrintMsg(
                '=============================================================')
                msg.PrintMsg(
                'WARNING: The icount for focus thread region pinball is low:')
                msg.PrintMsg('           Dir:               ' + dirname)
                msg.PrintMsg('           File:              ' + file_name)
                knobs += ' ' + self.kit_obj.knob_length + ' ' + '100'
                msg.PrintMsg('         No LIT will be generated')
            else:
                knobs += ' ' + self.kit_obj.knob_length + ' ' + str(lit_len)
            knobs += ' -log:early_out'
            knobs += ' -log:basename ' + lit_path_file_no_tid
            if hasattr(options, 'compressed'):
                knobs += ' -log:compressed ' + options.compressed
            knobs += util.GetMsgFileOption(lit_path_file + '.lit')

            if hasattr(options, 'list') and not options.list:
                msg.PrintMsgPlus('Generating LIT files:  ' + file_name)
            end_str = file_name

        # Format & execute the command.
        #
        cmd = cmd_0 + '"' + knobs + '"' + cmd_1

        # Run the command in the background in order to run concurrent jobs.
        #
        # import pdb;  pdb.set_trace()
        result = util.RunCmd(cmd, options, end_str, concurrent=True)

        return result

    def GenAllLitFiles(self, replay_cmd, options):
        """
        Generate LIT files and warmup LMAT files for all region pinballs in the tracing instance.
        """

        result = 0

        # Get the region pinball directories for this tracing instance.
        #
        # import pdb;  pdb.set_trace()
        pp_dirs = util.GetRegionPinballDir(options)

        # Put the replay_cmd, options and flavor of files to generate into a
        # directory because only one argument can be passed thru to the method
        # GenLitFiles(), but three arguments need to be given to this method.
        #
        # Set the flavor to LMAT file generation.
        #
        param = {
            'replay_cmd': replay_cmd,
            'options': options,
            'flavor': self.LMAT
        }

        # First generate LMAT files for each directory.
        #
        # import pdb;  pdb.set_trace()
        if hasattr(options, 'list') and not options.list:
            msg.PrintMsgDate('Generating LMAT files')
        for pdir in pp_dirs:
            if not options.list:
                msg.PrintMsgPlus(
                    'Generating LMAT files for pinballs in: ' + pdir)
            result = util.RunAllDir(pdir, self.GenLitFiles, True, param)
            if result != 0:
                msg.PrintMsg('Error found during LIT file generation (1)')
                return result

        # Need to wait until all the LMAT jobs are complete before the
        # LIT files are generated.
        #
        if hasattr(options, 'list') and not options.list:
            msg.PrintMsgPlus('Waiting on concurrent LMAT file generation')
        result = util.WaitJobs(options)
        if hasattr(options, 'list') and not options.list:
            msg.PrintMsgDate('Finished generating LMAT files')

        # Now set the flavor for LIT file generation.
        #
        param['flavor'] = self.LIT

        # Then generate LCAT files for each directory.
        #
        # import pdb;  pdb.set_trace()
        if hasattr(options, 'list') and not options.list:
            msg.PrintMsgDate('Generating LIT files')
        for pdir in pp_dirs:
            if hasattr(options, 'list') and not options.list:
                msg.PrintMsgPlus('Generating LIT files for pinballs in: ' + pdir)
            result = util.RunAllDir(pdir, self.GenLitFiles, True, param)
            if result != 0:
                msg.PrintMsg('Error found during LIT file generation (2)')
                return result

        # Need to wait until all the LIT jobs are complete before continuing.
        #
        if hasattr(options, 'list') and not options.list:
            msg.PrintMsgPlus('Waiting on concurrent LIT file generation')
        result = util.WaitJobs(options)
        if hasattr(options, 'list') and not options.list:
            msg.PrintMsgDate('Finished generating LIT files')

        return result

    ###################################################################
    #
    # Methods to verify LIT files with a simulator.
    #
    ###################################################################

    def VerifyLITFiles(self, sim_run_cmd, options):
        """
        Use a simulator to verify all the LIT files for the tracing instance are valid.
        """

        # Verify the LIT files for each directory.
        #
        # import pdb;  pdb.set_trace()
        result = 0
        for lit_dir in util.GetLitDir():
            if not options.list:
                msg.PrintMsgPlus('Verifying files in dir: ' + lit_dir)

            # import pdb;  pdb.set_trace()
            cmd = sim_run_cmd + ' --replay_dir ' + lit_dir
            if options.sim_options:
                cmd += ' --sim_options ' + options.sim_options
            cmd += ' --verify'
            cmd += util.AddGlobalFile(self.gv.DumpGlobalVars(), options)
            cmd += util.AddCfgFile(options)
            result = util.RunCmd(cmd, options, '',
                                 concurrent=True)  # Run jobs concurrently

            if result != 0:
                msg.PrintMsg('Error found during LIT file verification')
                return result

        # Wait for all the verification jobs to finish.
        #
        result = util.WaitJobs(options)

        return result

    ###################################################################
    #
    # Method to generate instruction mix files.
    #
    ###################################################################

    def GenImix(self, param, dirname, base_name):
        """
        Generate the instruction mix for one pinball.
        """

        # Define a lambda function to print a standard error msg.
        #
        err_msg = lambda string: msg.PrintMsg(
            'ERROR: method sde_phases.GenImix() failed to get '
            'field: ' + string)

        # Get local copies of items in param.
        #
        if param.has_key('options'):
            options = param['options']
        else:
            err_msg('options')
            return -1
        if param.has_key('replayer_cmd'):
            replayer_cmd = param['replayer_cmd']
        else:
            err_msg('replayer_cmd')
            return -1

        # Instruction mix output file name.
        #
        file_name = os.path.join(dirname, base_name)
        imix_file = file_name + '.imix.txt'

        # Format the command.
        #
        cmd = replayer_cmd + ' --replay_file ' + file_name
        if options.replay_options:  # Add any user defined replay options
            cmd += ' ' + options.replay_options
        cmd += ' --log_options '
        cmd += '"'
        cmd += ' -omix ' + imix_file

        # Assume if the user gives any knobs to SDE, they replace the
        # default knobs added here.
        #
        if options.pin_options == '':
            cmd += ' -top_blocks 100 '
        cmd += '"'

        if not options.list:
            msg.PrintMsgPlus('Generating imix for: ' + base_name)

        # Dump the global vars to a pickle file and add this option
        # to the command. Then do it.
        #
        cmd += util.AddGlobalFile(self.gv.DumpGlobalVars(), options)
        cmd += util.AddCfgFile(options)
        if hasattr(options, 'verbose') and options.verbose and not options.list:
            msg.PrintMsg('sde_phases.GenImix, calling RunCmd()')
        end_str = base_name
        # import pdb;  pdb.set_trace()
        result = util.RunCmd(cmd, options, end_str,
                             concurrent=True)  # Run concurrent jobs here

        return result
