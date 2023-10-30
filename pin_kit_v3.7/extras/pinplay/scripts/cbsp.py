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
# $Id: cbsp.py,v 1.11 2015/08/15 20:07:10 tmstall Exp tmstall $

# This is a script to generate traces using the CBSP method (Cross Binary SimPoint)
#

import ConfigParser
import re
import sys
import os
import optparse
import signal
import types

# Local modules
#
import cbsp_phases
import cmd_options
import config
import kit
import msg
import util
import pinpoints


class CBSP(pinpoints.PinPoints):
    """
    Top level script for generating traces using CBSP (Cross Binary Simpoints).

    Derived from the PinPlay tracing tool chain.

    IMPORTANT NOTE: Pin/PinPlay currently does not have the capability to run CBSP. This
    is only implemented in SDE.  However, the scripts are written so if CBSP is
    added to Pin/PinPlay users will be able to use this script to run it.

    Of course, this code can't be tested with Pin/PinPlay, so there may be some
    bugs to work out when CBSP is added to Pin/PinPlay.
    """

    # Objects used in this class.
    #
    phases = cbsp_phases.CBSPPhases()

    def default_phases(self, parser, group):
        """
        For Pin/PinPlay CBSP, redefine the option '--default_phases' instead
        of using the one defined in cmd_options.default_phases().

        @param parser Optparse object
        @param group Parser option group in which options are displayed

        @return No return value
        """

        method = cmd_options.GetMethod(parser, group)
        method(
            "--default_phases",
            dest="default_phases",
            action="store_true",
            help="Always run these default phases: log, cb_match, simpoint, and "
            "region_pinball. Additional phases are also run by default with SDE.")

    def AddAdditionalOptions(self, parser):
        """
        There are no additional options for PinPlay.  This is only a stub.

        @param parser Optparse object

        @return No return value
        """

        return

    def ParseCommandLine(self):
        """
        Process command line arguments and read in config file/globals.

        @return List of options parsed from command line
        """

        version = '$Revision: 1.11 $'
        version = version.replace(' ', '')
        ver = version.replace(' $', '')
        us = '%prog <phase> --cbsp_cfgs CFG_FILE,CFG_FILE[,CFG_FILE]  [options] '
        desc = 'This script uses Cross Binary SimPoint (CBSP) and PinPlay tools ' \
               'to create a set of equal-work traces from two, or more, binaries compiled from the same source.\n\n'\
               '' \
               'At least one command line option must be given to chose the phase(s) of the script to run. ' \
               'Use option \'--default_phases\' to run all phases. '  \
               'See section "Phase options" for a list of phases. \n\n'\
               '' \
               'The option \'--cbsp_cfgs\' is also required to run this scripts.  It contains a list of PinPlay configuration '\
               'files.  Each file defines one of the binaries to be traced.  (See \'sde_pinpoints.py -h\' for more info on PinPlay config files.)\n\n'\
               '' \
               'Optional master CBSP config files may be given with the option \'-cfg\'.  The parameters '\
               'defined in each master cfg file are used for every binary.  Master CBSP config files are not required.\n\n'\
               '' \
               'Master CBSP cfg files are processed in order.  '\
               'Command line options over-ride values read from master CBSP configuration files. '\
               'The long option name, '\
               'not the single letter option, must be used in the config files.\n\n'\
               '' \
               'The parameter \'cbsp_name\' MUST be defined in at least one configuration file in order '\
               'to run this script.  It may be defined in master/binary CBSP config files or by command line option.\n\n'\
               '' \
               'Example configuration file:'\
               '                                                            '\
               '--------------------------------------------'\
               '                                                            '\
               '[Parameters]'\
               '                                                            '\
               'cbsp_name:     lipack_test'\
               '                                                            '\
               'program_name:  linpack'\
               '                                                            '\
               'input_name:    avx'\
               '                                                            '\
               'command:       AVX_linpack 8'\
               '                                                            '\
               'mode:          st'\
               '                                                            '\
               '--------------------------------------------\n\n'\
               '' \
               'Advanced Usage: Since CBSP is an extension of PinPlay, it\'s possible to combine the two '\
               'tool chains to generate traces.   For example, you can use sde_pinplay.py to generate whole program '\
               'pinballs in parallel (possibly using NetBatch).  Then use the CBSP script to run the \'cb_match\' '\
               'and \'simpoint\' phases to select equivalent work clusters in each binary.  Finally, you can run '\
               'sde_pinpoints.py with NetBatch to generate region pinballs/LIT files in parallel.\n\n'\
               '' \
               'If using this method, you must use the configuration file for just one binary with each invocation of '\
               'the sde_pinpoints.py script.  Also, in order to combine these tools, you must add the option '\
               '\'--add_program_wp\' to each sde_pinpoints.py run.  This is required so the directories names '\
               'generated/used by the two chains are compatible.  '\
               '' 


        util.CheckNonPrintChar(sys.argv)
        parser = optparse.OptionParser(
            usage=us,
            description=desc,
            version=ver,
            formatter=cmd_options.BlankLinesIndentedHelpFormatter())

        # Command line options to control the tools behavior. These are not in any
        # of the option groups.
        #
        # import pdb;  pdb.set_trace()
        cmd_options.add_program_wp(parser)
        cmd_options.append_status(parser)
        cmd_options.cbsp_cfgs(parser)
        cmd_options.config_file(parser)
        cmd_options.debug(parser)
        cmd_options.delete(parser)
        cmd_options.delete_all(parser)
        # cmd_options.dir_separator(parser)  # Only works for PinPlay, need to fix for CBSP
        cmd_options.log_options(parser)
        cmd_options.pintool(parser)
        cmd_options.pin_options(parser)
        cmd_options.replay_options(parser)
        cmd_options.save_global(parser)
        cmd_options.verbose(parser)

        self.AddAdditionalOptions(parser)

        # Top level command line options in the default group allow the user
        # to choose the phases to run.
        #
        phase_group = cmd_options.PhaseGroup(parser)

        self.default_phases(parser, phase_group)
        cmd_options.log(parser, phase_group)
        cmd_options.cb_match(parser, phase_group)
        cmd_options.simpoint(parser, phase_group)
        cmd_options.region_pinball(parser, phase_group)

        self.AddAdditionalPhaseOptions(parser, phase_group)

        parser.add_option_group(phase_group)

        # Top level command line options in param group are used to set various
        # tracing parameters.
        #
        param_group = cmd_options.ParameterGroup(parser)

        cmd_options.cbsp_name(parser, param_group)
        cmd_options.input_name(parser, param_group)
        cmd_options.num_cores(parser, param_group)
        cmd_options.program_name(parser, param_group)
        self.KitOption(parser, param_group)

        parser.add_option_group(param_group)

        # Top level command line options which only apply to the Simpoint phase.
        #
        simpoint_phase_group = cmd_options.SimpointPhaseGroup(parser)

        cmd_options.maxk(parser, simpoint_phase_group)
        cmd_options.simpoint_options(parser, simpoint_phase_group)
        cmd_options.slice_size(parser, simpoint_phase_group)

        parser.add_option_group(simpoint_phase_group)

        # Top level command line options which only apply to the region pinball generation phase.
        #
        region_pb_phase_group = cmd_options.RegionPBPhaseGroup(parser)

        cmd_options.epilog_length(parser, region_pb_phase_group)
        cmd_options.prolog_length(parser, region_pb_phase_group)
        cmd_options.warmup_length(parser, region_pb_phase_group)

        parser.add_option_group(region_pb_phase_group)

        # Top level command line options in modify group modify the behavior of phases.
        #
        modify_group = cmd_options.ModifyGroup(parser)

        # These should go before the rest of the modify options.
        #
        # cmd_options.coop_pinball(parser, modify_group)
        cmd_options.cross_os(parser, modify_group)
        cmd_options.list(parser, modify_group)

        parser.add_option_group(modify_group)

        # Parse the command line options.
        #
        # import pdb;  pdb.set_trace()
        (options, args) = parser.parse_args()

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        # Check to make sure there was at least one command line option given.
        #
        # import pdb;  pdb.set_trace()
        if len(sys.argv) == 1:
            msg.PrintMsg(
                "\nERROR: Must use command line options to chose at least one phase to run.\n"
                "Use the option '--default_phases' to run the default phases. Use '--help' for more info.")
            util.CheckResult(-1, options,
                             'Check command line options - no phase given')

        # User must give option/parameter 'cbsp_cfgs' (unless deleting files).
        #
        # import pdb;  pdb.set_trace()
        if not options.delete_all and \
           (not hasattr(options, 'cbsp_cfgs') or not options.cbsp_cfgs):
            msg.PrintMsg(
                '\nERROR: Must give list of configuration files for the CBSP binaries\n'
                'using parameter \'--cbsp_cfgs\'.')
            util.CheckResult(-1, options, 'Check command line options - '
                             'Looking for parameter \'cbsp_cfgs\'.')

        # Get a list of the parameters for each CBSP binary.  
        #
        # import pdb;  pdb.set_trace()
        bin_options = self.Config.GetAllCBSPBinParams(options)

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        # Now that we have all the parameters, create a new status file or,
        # zero out the file if it already exists.  Need all the parameters
        # because 'cbsp_name' is used as part of the status file name. If user
        # sets parameter 'append_status', the old file is not deleted.
        #
        util.NewStatusFile(options, cbsp=True)

        # Ensure each binary has a unique program_name/input_name
        #
        all_names = set()
        for bopts in bin_options:
            name = '%s %s' % (bopts.program_name, bopts.input_name)
            if name in all_names:
                msg.PrintMsg(
                    '\nERROR: Must have unique program_name & input_name for '
                    'each binary.\n'
                    'Found duplicates: %s' % name)
                util.CheckResult(-1, options, 'Check command line options - '
                                 'Found duplicate program_name & input_name')
            all_names.add(name)

        # For debugging, print out the WP directory names for each binary
        #
        if hasattr(options, 'verbose') and options.verbose:
            for bopts in bin_options:
                msg.PrintMsg('WP dir: ' + util.GetDefaultWPDir(bopts))

        # Once the tracing configuration parameters are read, get the kit in
        # case sdehome/pinplayhome was set on the command line or in a config
        # file.  Also, need to reset paths to kit/scripts in object 'self' and
        # set the kit in self.phases.
        #
        kit_obj = self.GetKit()
        self.path = kit_obj.path
        self.script_path = kit_obj.script_path
        self.phases.SetKit(kit_obj)

        # If required, check to see if there any 'forbidden' char in some of
        # the parameters.
        #
        if self.kit_type == config.SDE and hasattr(options,
                                                   'spec') and not options.spec:
            self.Config.CheckForbiddenChar()

        # Do some 'special' things on native Windows.
        #
        util.WindowsNativeCheck(options)

        return options, bin_options

    def RunAdditionalPhases(self, sim_replay_cmd, options):
        """
        There are no additional phases for PinPlay.  This is only a stub.

        @param sim_replay_cmd Script which run the simulator on pinballs
        @param options Options given on cmd line
        @param options Options for each CBSP binary

        @return No return value
        """

        return

    def Run(self):
        """
        Get the user's options, read config file and execute the phases desired by the user.

        @return Exit code from last phase executed
        """

        # Catch signals in order to do an orderly shutdown of the script.
        #
        self.InstallSigHandler()

        # import pdb;  pdb.set_trace()
        options, bin_options = self.ParseCommandLine()

        # Print out the version numbers for all python modules in this directory.
        #
        if config.debug:
            util.PrintModuleVersions()

        # If logging, ensure the user has entered a mode.
        #
        if options.log and config.mode == '':
            msg.PrintHelpAndExit(
                'Must enter mode of application using: --mode "st|mt|mpi|mpi_mt"')

        # Set the defaults for a set of variables which are needed, but not in the list
        # of required options in the tracing configuration file.
        #
        self.SetDefaults(options)

        # Add 1500 to the warmup length in order to deal with problems that
        # occur when the start of a warmup region and the end of the previous
        # region are in the same basic block.
        #
        config.warmup_length += 1500

        # Get variables used in many phases.
        #
        # import pdb;  pdb.set_trace()
        wp_pb_dir = util.GetWPDir()
        status_file = util.GetCBSPLogFile()

        # Print out the tracing configuration parameters.
        #
        # TODO:  Fix print so it prints CBSP info, not PP info
        if not options.list:
            self.phases.PrintTracingInfo(bin_options, wp_pb_dir,
                                         status_file, self.PrintHome)

        #########################################################################
        #
        # Several phases which clean up tracing instances.
        #
        #########################################################################

        result = 0
        if options.delete:
            self.phases.DeleteTracingFileDir(options)
            util.CheckResult(result, options, 'Deleting tracing instance files')

            # Since just deleted all files/dirs for the tracing instance, need
            # to set the whole program directory to the default value.  Do NOT
            # use util.GetWPDir() here because this may return the name of a
            # relogged WP directory (which we, of course, just deleted.)
            #
            wp_pb_dir = util.GetBaseWPDir()

        if options.delete_all:
            self.phases.DeleteAllTracingFileDir(options)
            util.CheckResult(result, options, 'Deleting all tracing files')

            # Again, since just deleted all files/dirs, need to set the whole
            # program directory to the default value.  
            #
            wp_pb_dir = util.GetBaseWPDir()

        #########################################################################
        #
        # Run the phases of the script. They are in a specific order to ensure
        # all prerequisite phases are completed before a given phase is run.
        #
        #########################################################################

        # Logging phase to generate initial whole program pinballs.
        #
        # import pdb;  pdb.set_trace()
        if options.log or options.default_phases:
            util.PhaseBegin(options)
            result = 0

            # First check to make sure there aren't any old WP pinball directories.
            #
            for bopts in bin_options:
                wp_pb_dir = util.GetDefaultWPDir(bopts)
                if os.path.isdir(wp_pb_dir):
                    msg.PrintMsg(
                        "\nERROR: Whole program pinball directory already exists: "
                        + wp_pb_dir +
                        "\nRemove this directory to proceed with generating "
                        "whole program pinballs.")
                    util.CheckResult(-1, options, 'Whole program pinball generation %s' % \
                        config.PhaseStr(config.log_whole))

            for bopts in bin_options:

                # Add CSBP logging specific knobs to any user defined
                # log_options. They are required by this phase in order to
                # generate DCFG files.
                #
                if not hasattr(options, 'log_options'):
                    setattr(options, 'log_options', '')
                dcfg_log_opts = ' -omix %s.%s.mix' % (bopts.program_name, bopts.
                                                      input_name)
                dcfg_log_opts += config.cbsp_base_log_options
                if bopts.log_options:
                    bopts.log_options += dcfg_log_opts
                else:
                    bopts.log_options = dcfg_log_opts

                # Run the logger for this binary
                #
                wp_pb_dir = util.GetDefaultWPDir(bopts)
                log_file_name = util.GetLogFile(bopts)
                self.Config.SetPerBinParams(bopts)
                result = self.phases.Logger(self.logger_cmd, wp_pb_dir,
                                            log_file_name, bopts)
                util.CheckResult(result, options, 'WP pinballs for: %s %s' % \
                    (log_file_name, config.PhaseStr(config.log_whole)), intermediate_phase=True)

                # Now remove the DCFG log options as they are only used in this phase.  This
                # ensures only the user defined options are used for subsequent phases.
                #
                bopts.log_options = bopts.log_options.replace(dcfg_log_opts, '')

            util.CheckResult(result, options, 'Whole program pinball generation %s' % \
                config.PhaseStr(config.log_whole))

        # All phases after this require the whole program pinball directories.  Exit with an
        # error if they don't not exist.
        #
        # import pdb;  pdb.set_trace()
        if not options.list and not options.debug and\
           not options.delete and not options.delete_all:
            err_msg = lambda string: msg.PrintMsg('\nERROR: Can\'t proceed because whole program pinball directory does not exist:\n' + \
                '      ' + string + \
                '\nMust select at least one phase to run.  Try using option \'--default_phases\'.' + \
                '\nUse \'-h\' for help on selecting phases to run.')
            for bopts in bin_options:
                wp_dir = util.GetDefaultWPDir(bopts)
                if not os.path.isdir(wp_dir):
                    err_msg(wp_dir)
                    # Use -1 to force check to fail
                    util.CheckResult(
                        -1, options,
                        'Initial check to see if WP pinballs exist')

        # Print out the number of instructions in the whole program pinballs.
        #
        if not options.list:
            for bopts in bin_options:
                wp_dir = util.GetDefaultWPDir(bopts)
                msg.PrintMsg('')
                msg.PrintMsg(wp_dir)
                util.PrintInstrCount(util.GetDefaultWPDir(bopts), options)

        #########################################################################
        #
        # These phases are run with the whole progam pinballs defined/generated
        # in the previous phase.
        #
        #########################################################################

        # Run the cross binary matcher.
        #
        if options.cb_match or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Running cross binary matcher %s' % \
                    config.PhaseStr(config.cb_match))
                msg.PrintMsg('')
            util.PhaseBegin(options)
            result = self.phases.CrossBinaryMatcher(self.path, self.script_path,
                                                    options, bin_options)
            if not options.list:
                msg.PrintMsgDate('Finished running cross binary matcher %s' % \
                    config.PhaseStr(config.cb_match))
            util.CheckResult(result, options, 'Cross binary matcher %s' % \
                config.PhaseStr(config.cb_match))

        # Run Simpoints to generate clusters
        #
        if options.simpoint or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Running Simpoint %s' % \
                    config.PhaseStr(config.Simpoint))
            param = {'options': options}
            util.PhaseBegin(options)
            result = self.phases.RunSimPoint(self.path, self.script_path,
                                             options, bin_options)
            if not options.list:
                msg.PrintMsgDate('Finished running Simpoint %s' % \
                    config.PhaseStr(config.Simpoint))
            util.CheckResult(result, options, 'Simpoints generation %s' % \
                config.PhaseStr(config.Simpoint))

        # Relog to generate representative region pinballs.
        #
        if options.region_pinball or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Generating region pinballs %s' % \
                    config.PhaseStr(config.relog_regions))
            util.PhaseBegin(options)
            result = 0
            for bopts in bin_options:
                wp_dir = util.GetDefaultWPDir(bopts)
                log_file_name = util.GetLogFile(bopts)
                self.Config.SetPerBinParams(bopts)
                result = self.phases.MultiIterGenRegionPinballs(
                    wp_dir, self.replayer_cmd, bopts)
                if result == 0:
                    if not options.list:
                        msg.PrintMsgPlus(
                            'Waiting on final concurrent region pinball generation')
                    result = util.WaitJobs(options)
                util.CheckResult(result, options, 'Region pinballs for: %s %s' % \
                    (log_file_name, config.PhaseStr(config.relog_regions)), intermediate_phase=True)
            if not options.list:
                msg.PrintMsgDate('Finished generating region pinballs %s' % \
                    config.PhaseStr(config.relog_regions))
            util.CheckResult(result, options, 'Region pinball generation %s' %\
                config.PhaseStr(config.relog_regions))

        # If there are any additional phases, then run them.
        #
        self.RunAdditionalPhases(self.sim_replay_cmd, options, bin_options)

        # Cleanup and print out a string to indicate the tracing has completed.
        #
        # import pdb;  pdb.set_trace()
        util.CleanupTraceEnd(options)
        util.PrintTraceEnd(options)

        return result


def main():
    """
    Process command line arguments and run the script

    @return Exit code from running the script
    """

    cb = CBSP()
    result = cb.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    if (result > 0):
        sys.exit(result)
