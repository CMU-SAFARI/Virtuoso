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
# $Id: cbsp_phases.py,v 1.15 2015/08/15 20:05:49 tmstall Exp tmstall $

# Print out messages, including error messages
#
#

import glob
import os
import re
import shutil
import subprocess
import sys

# Local modules
#
import config
import msg
import phases
import util


class CBSPPhases(phases.Phases):
    """
    Module contains CBSP (Cross Binary SimPoint) methods implementing phases
    not defined for PinPlay.
    """

    def PrintTracingInfo(self, bin_options, wp_pb_dir, status_file,
                         PrintHome):
        """
        Print parameters for both the master CBSP and for each binary.

        It's very handy to have this info to see what was actually used in the
        run. Often it's not quite what you think it is.  :-)  Also, useful for
        debugging.

        @param bin_options List of options for each CBSP binary
        @param wp_pb_dir Whole program pinball directory
        @param status_file Status file name
        @param PrintHome Pointer to function which prints 'home' dir for kit

        @return no return
        """

        import time
        import locale

        pr_str = '***  CBSP: START' + '  ***    ' + time.strftime(
            '%B %d, %Y %H:%M:%S')
        msg.PrintMsg(pr_str)
        locale.setlocale(locale.LC_ALL, "")

        # RCS version of this script.
        #
        version = '$Revision: 1.15 $'
        version = version.replace('$', '')
        msg.PrintMsg(version)

        # Arguments passed to the script.
        #
        script = os.path.basename(sys.argv[0])
        msg.PrintMsg('Script:                    ' + script),
        msg.PrintMsgNoCR('Script args:               '),
        for string in sys.argv[1:]:
            msg.PrintMsgNoCR(string + ' '),
        msg.PrintMsg('')

        # If we don't have any info on binaries in bin_options, just return
        #
        if not bin_options:
            return

        # Print some files/paths which are useful to know.
        # Use 'bin_options[0]' because parameter should be the same for all
        # binaries.
        #
        if hasattr(bin_options[0], 'pintool') and bin_options[0].pintool:
            msg.PrintMsg('Pintool:                   ' + bin_options[0].pintool)
        msg.PrintMsg('CBSP instance name:        %s' %
                     (bin_options[0].cbsp_name))
        msg.PrintMsg('Instance status file:      %s.info.status' %
                     (status_file))
        PrintHome(bin_options[0])  # Kit location
        msg.PrintMsg('Script path:               ' + util.GetScriptDir())
        msg.PrintMsg('Working dir:               ' + os.getcwd())

        # CBSP parameters.
        #
        for bopts in bin_options:
            msg.PrintMsg('')
            msg.PrintMsg('Program name:              ' + bopts.program_name)
            msg.PrintMsg('Input name:                ' + bopts.input_name)
            # import pdb;  pdb.set_trace()
            if hasattr(bopts, 'command') and bopts.command:
                msg.PrintMsg('Command:                   ' + bopts.command)
            if hasattr(bopts, 'mode') and bopts.mode:
                msg.PrintMsg('Tracing mode:              ' + bopts.mode)
            if hasattr(bopts, 'maxk') and bopts.maxk:
                msg.PrintMsg('Maxk:                      ' + str(bopts.maxk))
            if hasattr(bopts, 'log_options') and bopts.log_options:
                msg.PrintMsg('Logger options:            ' + bopts.log_options)
            if hasattr(bopts, 'replay_options') and bopts.replay_options:
                msg.PrintMsg(
                    'Replay options:            ' + bopts.replay_options)
            if hasattr(bopts, 'pin_options') and bopts.pin_options:
                msg.PrintMsg('Pin options:               ' + bopts.pin_options)
            if hasattr(bopts, 'prolog_length'):
                msg.PrintMsg('Prolog length:             ' + \
                    locale.format('%d', int(bopts.prolog_length), True))
            if hasattr(bopts, 'warmup_length'):
                msg.PrintMsg('Warmup length:             ' + \
                    locale.format('%d', int(bopts.warmup_length), True))
            if hasattr(bopts, 'slice_size'):
                msg.PrintMsg('Slice size (region):       ' + \
                    locale.format('%d', int(bopts.slice_size), True))
            if hasattr(bopts, 'epilog_length'):
                msg.PrintMsg('Epilog length:             ' + \
                    locale.format('%d', int(bopts.epilog_length), True))
            if hasattr(bopts, 'dir_separator') and bopts.dir_separator:
                msg.PrintMsg('Dir separator:             ' + bopts.dir_separator)
            msg.PrintMsg(
                'WP pinball directory:      ' + util.GetDefaultWPDir(bopts))

            # Print the generic directory name for Data/pp/lit and
            # generic trace names.
            #
            dirs = util.GetDataDir(bopts)
            dirs = [p.replace('.Data', '') for p in dirs]
            if len(dirs) != 0:
                if len(dirs) == 1:
                    msg.PrintMsg('Data/lit/pp directory:     ' + dirs[0])
                else:
                    msg.PrintMsg('Data/lit/pp dirs:          ' + dirs[0])
                    del dirs[0]
                    for d in dirs:
                        msg.PrintMsg('                           ' + str(d))

                # Default to thread 0.   Must change this if modifying code to work
                # with multiple threads.
                #
                thread = 0
                msg.PrintMsg('Trace file name format:    ' + dirs[0] + '_t' + str(thread) + \
                    'rX_warmup' + str(bopts.warmup_length) + '_prolog' + str(bopts.prolog_length) + \
                    '_region' + str(bopts.slice_size) + '_epilog' + str(bopts.epilog_length))

            # Info about cores used for CBSP and total number in system.
            #
            if hasattr(bopts, 'num_cores') and bopts.num_cores:
                msg.PrintMsg(
                    'Num cores:                 ' + str(bopts.num_cores))
            msg.PrintMsg('Number cores/system:       ' + str(util.NumCores()))

        # Print some useful environment variables.
        #
        if os.environ.has_key('PATH'):
            msg.PrintMsg('')
            msg.PrintMsg('PATH: ' + os.environ['PATH'])
        if os.environ.has_key('LD_LIBRARY_PATH'):
            msg.PrintMsg('')
            msg.PrintMsg('LD_LIBRARY_PATH: ' + os.environ['LD_LIBRARY_PATH'])

    def DeleteTracingFileDir(self, options):
        """
        Delete files & directories generated for only the current tracing instance.

        TODO: Implement this for CBSP.

        @param options Options given on cmd line

        @return no return
        """

        # The "base" WP pinball directory is the directory before any filters have
        # been applied.
        #
        # import pdb;  pdb.set_trace()
        wp_pb_dir = util.GetBaseWPDir()

        # Variables used to define different file types for this tracing instance.
        #
        relog_wp_dir = wp_pb_dir + config.relog_dir_str
        log_file = util.GetLogFile()
        pb_list = [p.replace('.Data', '') for p in util.GetDataDir()]
        csv_list = [p + '*.csv' for p in pb_list]
        sim_list = [p + '_simpoint_out.txt' for p in pb_list]

        # Only delete these files/dirs which are specific to the current
        # tracing instance defined by tracing parameters.
        #
        util.Delete(options, ' '.join(util.GetDataDir()))
        util.Delete(options, ' '.join(util.GetRegionPinballDir()))
        util.Delete(options, ' '.join(util.GetLitDir()))
        util.Delete(options, relog_wp_dir + '.*')
        util.Delete(options, ' '.join(csv_list))
        util.Delete(options, ' '.join(sim_list))
        util.Delete(options, log_file + '*.NATIVE.TIME')
        util.Delete(options, 'verify_' + log_file + '_out.txt')
        util.Delete(options, util.GetStatusFileName())
        util.Delete(options, 'PARALLEL.PARAM')

        # Delete sniper result directories for this tracing instance
        #
        sniper_dirs = glob.glob(os.path.join(config.sniper_result_dir,
                                             wp_pb_dir,
                                             util.GetLogFile() + '*'))
        sniper_dirs += [
            os.path.join(config.sniper_result_dir, os.path.basename(w) + '.pp')
            for w in sniper_dirs
        ]
        for dir in sniper_dirs:
            util.Delete(options, dir)
        util.Delete(options, os.path.join(config.sniper_result_dir, wp_pb_dir))

        # Only delete the whole program pinball directory if it was not
        # specified by the user in a previous run of the scripts or the user
        # gave the option '--delete_wp'. 
        #
        # import pdb;  pdb.set_trace()
        if (hasattr(config, 'whole_pgm_dir') and config.whole_pgm_dir == '') or \
           options.delete_wp:
            util.Delete(options,
                        self.Config.GetInstanceFileName(config.config_ext))
            util.Delete(options, wp_pb_dir)
        else:
            msg.PrintMsg(
                '\nNOTE: User defined whole program directory was used.  Will NOT\n'
                'delete this directory.')
            msg.PrintMsg('    ' + config.whole_pgm_dir)

        # No error checking for deleting
        #
        return 0

    def DeleteAllTracingFileDir(self, options):
        """
        Delete ALL files & directories generated for all tracing instances.

        @param options Options given on cmd line

        @return no return
        """

        util.Delete(options, config.wp_dir_basename + '*')
        util.Delete(options, '*.Data')
        util.Delete(options, '*.pp')
        util.Delete(options, '*.lit')
        util.Delete(options, '*.csv')
        util.Delete(options, '*.bb')
        util.Delete(options, '*.NATIVE.TIME')
        util.Delete(options, '*.traceinfo*')
        util.Delete(options, '*.procinfo*')
        util.Delete(options, '*_simpoint_out.txt')

        util.Delete(options, 'update_tracinfo_files')
        util.Delete(options, 'PARALLEL.PARAM')
        util.Delete(options, 'verify_*_out.txt')
        util.Delete(options, 'global.data.*')
        util.Delete(options, '*.info.*')
        util.Delete(options, config.sniper_result_dir)

    def CrossBinaryMatcher(self, kit_path, script_path, options, bin_options):
        """
        Run the cross binary matcher on all the binaries in the CBSP experiment.

        NOTE: Only works if there is only one WP pinball in each WP pinball
        directory (i.e. will NOT work with multipe process applications).

        @param options Options given on cmd line
        @param kit_path Path to kit
        @param script_path Explicit path to location in kit where scripts are located
        @param bin_options List of options for each CBSP binary

        @return error_code
        """

        # Get CBSP Data directory and WP pinball basenames.  NOTE: For expanding
        # this code to work with multi-process WP pinballs, need to expand list
        # returned by util.GetWPPinballs() to include all pinballs in each WP
        # dir.
        #
        result = 0
        cbsp_data_dir = util.GetCBSPDataDir(options)
        wp_pinballs = []
        for bopt in bin_options:
            pb = util.GetWPPinballs(bopt)
            if not pb:
                return -1
            wp_pinballs.append(pb[0])
        wp_basenames = [os.path.join(re.sub('whole_program.*/', '', pb))
                        for pb in wp_pinballs]

        # Go to CBSP Data directory to run matcher
        #
        orig_dir = os.getcwd()
        if os.path.isdir(cbsp_data_dir):
            os.chdir(cbsp_data_dir)
        else:
            msg.PrintMsg('ERROR: Unable to change to CBSP Data directory: ' +
                         cbsp_data_dir)
            return -1

        # Matcher output file
        #
        output_file = 'crossBinaryMatcher_out.txt'
        try:
            fp_out = open(output_file, 'w')
        except IOError:
            msg.PrintMsg(
                'ERROR: Failed to open cross binary matcher output file:\n'
                '   ' + output_file)
            return -1

        # Format cross binary matcher command & execute.
        # Use 'bin_options[0]' because parameter should be the same for all
        # binaries.
        #
        if not options.list:
            msg.PrintMsgDate(
                'Running cross binary matcher for: ' + options.cbsp_name)
            msg.PrintMsgPlus('Cross binary matcher output file (including errors): %s\n' %
                             os.path.join(cbsp_data_dir, output_file))
        cmd = 'crossBinMatcher -graphm_config %s' % os.path.join(
            kit_path, script_path, 'graphm.config.txt')
        cmd += ' -slice_size %s' % str(bin_options[0].slice_size)
        for pb, basename in zip(wp_pinballs, wp_basenames):
            cmd += ' %s.{dcfg.json,trace.json}.bz2 ' % os.path.join('..', pb)
            cmd += ' %s.bb-profile.bz2 ' % (basename)
        if options.list or options.debug:
            msg.PrintMsg(cmd)
        else:
            result = util.RunCmd(cmd, options, '',
                                 f_stdout=fp_out,
                                 f_stderr=fp_out)
            fp_out.close()

        if not options.list:
            msg.PrintMsgDate(
                'Finished running cross binary matcher for: ' + options.cbsp_name)

        # Return to original directory
        #
        os.chdir(orig_dir)

        return result

    def RunSimPoint(self, kit_path, script_path, options, bin_options):
        """
        Run Simpoint in the CBSP Data directory and generate weight files for
        each binary in the respective binary Data directory.

        @param kit_path Path to kit
        @param script_path Explicit path to location in kit where scripts are located
        @param options Options given on cmd line
        @param bin_options List of options for each CBSP binary

        @return exit code from Simpoint
        """

        # Get CBSP Data directory and WP pinball basenames.  For multi-process
        # need to expand list returned by util.GetWPPinballs() to include all
        # pinballs in each WP dir.
        #
        result = 0
        cbsp_data_dir = util.GetCBSPDataDir(options)
        wp_pinballs = [util.GetWPPinballs(bopt)[0] for bopt in bin_options]
        wp_basenames = [os.path.join(re.sub('whole_program.*/', '', pb))
                        for pb in wp_pinballs]

        # Go to CBSP Data directory to run Simpoint
        #
        orig_dir = os.getcwd()
        if os.path.isdir(cbsp_data_dir):
            os.chdir(cbsp_data_dir)
        else:
            msg.PrintMsg('ERROR: Unable to change to CBSP Data directory: ' +
                         cbsp_data_dir)
            return -1

        # Format the command to run simpoints.
        # Use 'bin_options[0]' because parameter should be the same for all
        # binaries.
        #
        sim_out_file = 'run_simpoint_out.txt'
        if not options.list:
            msg.PrintMsgDate('Running Simpoints for: %s' % options.cbsp_name)
            msg.PrintMsgPlus('Simpoint output file (including errors): %s\n' %
                             os.path.join(cbsp_data_dir, sim_out_file))
        if bin_options[0].simpoint_options:
            msg.PrintMsgPlus(
                'NOTE: Default options for Simpoint not used, only user defined options.')
            cmd = 'simpoint ' + bin_options[0].simpoint_options
        else:
            if bin_options[0].maxk:
                cmd = 'simpoint -k 2:%d -dim 100 -numInitSeeds 25 -fixedLength off -iters 500' % bin_options[0].maxk
            else:
                cmd = 'simpoint -k 2:25 -dim 100 -numInitSeeds 25 -fixedLength off -iters 500'
            cmd += ' -saveLabels labels.txt -saveSimpoints simpoints.txt -inputVectorsGzipped -loadFVFile matching-vec-profile.gz'
        if options.list or options.debug:
            msg.PrintMsg(cmd)
        else:
            fp_out = open(sim_out_file, 'w')
            result = util.RunCmd(cmd, options, '',
                                 f_stdout=fp_out,
                                 f_stderr=fp_out)
            fp_out.close()
            if result != 0:
                msg.PrintMsg('\nError found while running Simpoint in dir:\n'
                             '   %s' % os.getcwd())
                return result

        # Generate the binary Data directories
        #
        bin_data_dir = []
        for basename in wp_basenames:
            name = os.path.join('..', '%s.Data' % basename)
            bin_data_dir.append(name)
            if not os.path.isdir(name):
                try:
                    os.mkdir(name)
                except OSError:
                    msg.PrintAndExit(
                        'method RunSimPoint(), Unable to make directory: ' + name)

        # Run command to generate weight files for the binaries
        #
        weight_out_file = 'generate_weights_out.txt'
        cmd = os.path.join('make_simpoint_weights.py --weight_file_list ')
        for data_dir in bin_data_dir:
            cmd += ' %s/weights.txt' % data_dir
        if not options.list:
            msg.PrintMsgPlus('make_simpoint_weights.py output file (including errors): %s\n' %
                             os.path.join(cbsp_data_dir, weight_out_file))
        if options.list or options.debug:
            msg.PrintMsg(cmd)
        else:
            fp_out = open(weight_out_file, 'w')
            result = util.RunCmd(cmd, options, '',
                                 f_stdout=fp_out,
                                 f_stderr=fp_out)
            fp_out.close()
            if result != 0:
                msg.PrintMsg('\nError found while running make_simpoint_weights.py in dir:\n'
                             '   %s' % os.getcwd())
                return result

        # Copy the simpoints and labels files to binary Data directories
        #
        #
        def copy_file(f, d):

            try:
                shutil.copy(f, d)
                return 0
            except IOError:
                msg.PrintMsg('\nError found in dir:\n'
                    '    %s\nUnable to copy file:\n    %s to %s' %
                             (os.getcwd(),f, d))
                return -1

        for data_dir in bin_data_dir:
            result = copy_file('simpoints.txt', data_dir)
            result = result | copy_file('labels.txt', data_dir)
            if result != 0:
                return result

        # Generate the CSV files in each binary Data directory
        #
        for data_dir, basename in zip(bin_data_dir, wp_basenames):
            # Go to the binary Data directory
            #
            old_dir = os.getcwd()
            os.chdir(data_dir)

            # Run the script to generate CSV files for this binary
            #
            bb_prof = os.path.join('..', cbsp_data_dir,
                                   '%s.bb-profile.bz2' % basename)
            csv_file = '%s.pinpoints.csv' % basename
            if not options.list:
                data_dir_rel_path = os.getcwd().replace(os.path.join(orig_dir, ''), '')
                msg.PrintMsgPlus('Any errors from running \'regions.py\' are in: %s' %
                                 os.path.join(data_dir_rel_path, csv_file))
            cmd = os.path.join('regions.py --csv_region --bbv_file %s' % bb_prof)
            cmd += ' --region_file=simpoints.txt --weight_file=weights.txt'
            cmd += ' > %s 2>&1' % csv_file
            msg.PrintMsg('')
            if options.list or options.debug:
                msg.PrintMsg(cmd)
            else:
                result = util.RunCmd(cmd, options, '')
                if result != 0:
                    msg.PrintMsg('\nError found while generating CSV files in:\n   %s' % os.getcwd())
                    msg.PrintMsg('Error msgs in file: %s ' % csv_file)
                    return result

            # Return to the CBSP Data directory
            #
            os.chdir(old_dir)

        # Return to original directory
        #
        os.chdir(orig_dir)
        if not options.list:
            msg.PrintMsgDate('Finished running Simpoint for: ' + options.cbsp_name)

        return result
