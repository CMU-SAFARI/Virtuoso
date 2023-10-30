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
# $Id: pinpoints.py,v 1.100 2015/08/15 20:06:41 tmstall Exp tmstall $

# This is a script to replay one pinplay process
#

import sys
import os
import optparse
import signal

# Local modules
#
import cmd_options
import config
import kit
import msg
import phases
import util


class PinPoints(object):
    """
    Top level script for tracing with Pin/Pinplay.
    """

    # Objects used in this class.
    #
    phases = phases.Phases()
    Config = config.ConfigClass()

    # What type of a kit is this?
    #
    kit_type = None

    # Store several attributes from the kit that are needed.
    #
    pin = ''
    path = ''
    script_path = ''

    # Python scripts used by this top level script.
    #
    logger_cmd = 'logger.py'
    replay_cmd = 'replay_dir.py'
    replayer_cmd = 'replayer.py'
    sim_replay_cmd = ''

    def PrintHome(self, options):
        """
        Print the directory where the PinPlay kit is located.

        @return No return value
        """

        # If this variable is a symbolic link, then print the real directory name.
        #
        if os.path.islink(self.path):
            path = os.path.realpath(self.path)
        else:
            path = self.path
        msg.PrintMsg('Pinplayhome:               ' + path)

    def KitOption(self, parser, param_group):
        """
        Add the PinPlay kit option to the current list of options.

        @return No return value
        """

        cmd_options.pinplayhome(parser, param_group)

        return

    def GetKit(self):
        """
        Get the PinPlay kit.

        @return Pinplay kit object
        """

        kit_obj = kit.Kit()
        self.pin = kit_obj.pin
        self.kit_type = kit_obj.kit_type

        return kit_obj

    def HandleSig(self, signal, frame):
        """
        Clean up when a signal is sent and exit.

        @param signal Signal to handle
        @param frame  Not used

        @return No return value, calls system exit
        """

        # msg.PrintMsg('\nSignal caught, cleaning up.')
        util.CleanupTraceEnd()
        util.PrintTraceEnd()
        sys.exit(0)

    def InstallSigHandler(self):
        """
        Install a signal handler to make a clean exit.

        @return No return value
        """

        for sig in (signal.SIGABRT, signal.SIGILL, signal.SIGINT,
                    signal.SIGSEGV, signal.SIGTERM):
            signal.signal(sig, self.HandleSig)

    def AddAdditionalOptions(self, parser):
        """
        There are no additional options for PinPlay.  This is only a stub.

        @return No return value
        """

        return

    def AddAdditionalPhaseOptions(self, parser, phase_group):
        """
        There are no additional 'phase' options for PinPlay.  This is only a stub.

        @return No return value
        """

        return

    def AddAdditionalModifyOptions(self, parser, phase_group):
        """
        There are no additional 'modify' options for PinPlay.  This is only a stub.

        @return No return value
        """

        return

    def AddVerifyOptions(self, parser):
        """
        There are no verify options for PinPlay.  This is only a stub.

        @return No return value
        """

        return

    def WindowsNativeCheck(self, options):
        """
        If running on native Windows environment, the scripts currently only
        run on one core.  Force 'num_cores' to be 1 on this platform.

        Restriction to be removed once the scripts work on more cores.

        @return No return value
        """

        platform = util.Platform()
        if platform == config.WIN_NATIVE and hasattr(options, 'num_cores'):
            options.num_cores = config.num_cores = 1

        return

    def ParseCommandLine(self):
        """
        Process command line arguments and read in config file/globals.

        @return List of options parsed from command line
        """

        version = '$Revision: 1.100 $'
        version = version.replace(' ', '')
        ver = version.replace(' $', '')
        us = '%prog phase [options] \nVersion: ' + ver
        desc = 'The script generates traces for an application using the PinPoints methodology '\
               '(based on PinPlay: Pin-based record/replay tools). '\
               'At least one command line option must be given to chose the phases to run. ' \
               'Use option \'--default_phases\' to run all the default phases. '  \
               'See section "Phase options" for a list of phases. \n\n'\
               '' \
               'The two parameters \'program_name\' and \'input_name\' must be defined in order '\
               'to run this script.  If running the logging phase (-l), then two more parameters, '\
               '\'command\' and \'mode\', must also be defined.  '\
               'Parameters can be given either in a tracing configuration file or with command '\
               'line options.  \n\n'\
               '' \
               'The default configuration file is "tracing.cfg". '\
               'If it exists, the script will always read this cfg file first. '\
               'Use the option "--cfg" to select additional files. '\
               'Each cfg file on the command line is processed in order.  '\
               'Command line options over-ride values read from configuration files. '\
               'All parameters listed in the three parameter '\
               'sections below can be defined in a cfg file.  The long option name, '\
               'not the single letter option, must be used in the cfg files.\n\n'\
               '' \
               'Example parameter configuration file:'\
               '                                                            '\
               '--------------------------------------------'\
               '                                                            '\
               '[Parameters]'\
               '                                                            '\
               'program_name:   omnetpp'\
               '                                                            '\
               'input_name:     p10000-s10'\
               '                                                            '\
               'command:        ./dtlb5-lin64 -p10000 -s10'\
               '                                                            '\
               'mode:           st'\
               '                                                            '\
               '--------------------------------------------'

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
        cmd_options.config_file(parser)
        cmd_options.debug(parser)
        cmd_options.delete(parser)
        cmd_options.delete_all(parser)
        cmd_options.delete_wp(parser)
        cmd_options.dir_separator(parser)
        cmd_options.log_options(parser)
        cmd_options.msgfile_ext(parser)
        cmd_options.no_glob(parser)
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

        cmd_options.default_phases(parser, phase_group)
        cmd_options.native_pure(parser, phase_group)
        cmd_options.log(parser, phase_group)
        cmd_options.replay(parser, phase_group)
        cmd_options.basic_block_vector(parser, phase_group)
        cmd_options.simpoint(parser, phase_group)
        cmd_options.region_pinball(parser, phase_group)
        cmd_options.replay_region(parser, phase_group)

        self.AddAdditionalPhaseOptions(parser, phase_group)

        parser.add_option_group(phase_group)

        # Top level command line options in param group are used to set various
        # tracing parameters.
        #
        param_group = cmd_options.ParameterGroup(parser)

        cmd_options.command(parser, param_group)
        cmd_options.compressed(parser, param_group)
        cmd_options.focus_thread(parser, param_group)
        cmd_options.input_name(parser, param_group)
        cmd_options.mpi_options(parser, param_group)
        cmd_options.mode(parser, param_group)
        cmd_options.num_cores(parser, param_group)
        cmd_options.num_proc(parser, param_group)
        cmd_options.program_name(parser, param_group)
        self.KitOption(parser, param_group)

        parser.add_option_group(param_group)

        # Top level command line options which only apply to the Simpoint phase.
        #
        simpoint_phase_group = cmd_options.SimpointPhaseGroup(parser)

        cmd_options.combine(parser, simpoint_phase_group)
        cmd_options.cutoff(parser, simpoint_phase_group)
        cmd_options.ldv(parser, simpoint_phase_group)
        cmd_options.maxk(parser, simpoint_phase_group)
        cmd_options.slice_size(parser, simpoint_phase_group)
        cmd_options.warmup_factor(parser, simpoint_phase_group)
        cmd_options.pccount_regions(parser, simpoint_phase_group)
        cmd_options.global_regions(parser, simpoint_phase_group)
        cmd_options.simpoint_options(parser, simpoint_phase_group)

        parser.add_option_group(simpoint_phase_group)

        # Top level command line options which only apply to the region pinball generation phase.
        #
        region_pb_phase_group = cmd_options.RegionPBPhaseGroup(parser)

        cmd_options.epilog_length(parser, region_pb_phase_group)
        cmd_options.prolog_length(parser, region_pb_phase_group)
        cmd_options.warmup_length(parser, region_pb_phase_group)

        parser.add_option_group(region_pb_phase_group)

        # Add verify options.
        #
        self.AddVerifyOptions(parser)

        # Top level command line options in modify group modify the behavior of phases.
        #
        modify_group = cmd_options.ModifyGroup(parser)

        # These should go before the rest of the modify options.
        #
        cmd_options.coop_pinball(parser, modify_group)
        cmd_options.cross_os(parser, modify_group)
        cmd_options.list(parser, modify_group)
        cmd_options.native_pin(parser, modify_group)
        cmd_options.no_focus_thread(parser, modify_group)
        cmd_options.whole_pgm_dir(parser, modify_group)

        self.AddAdditionalModifyOptions(parser, modify_group)

        parser.add_option_group(modify_group)

        # Top level command line options in which only apply to the region pinball generation phase.
        #
        wp_filter_group = cmd_options.WPFilterGroup(parser)

        cmd_options.relog_name(parser, wp_filter_group)
        cmd_options.relog_focus(parser, wp_filter_group)
        cmd_options.relog_no_init(parser, wp_filter_group)
        cmd_options.relog_no_cleanup(parser, wp_filter_group)
        cmd_options.relog_code_exclude(parser, wp_filter_group)
        cmd_options.relog_no_omp_spin(parser, wp_filter_group)
        cmd_options.relog_no_mpi_spin(parser, wp_filter_group)
        cmd_options.use_relog_name(parser, wp_filter_group)
        cmd_options.use_relog_focus(parser, wp_filter_group)
        cmd_options.use_relog_no_cleanup(parser, wp_filter_group)
        cmd_options.use_relog_no_init(parser, wp_filter_group)
        cmd_options.use_relog_code_exclude(parser, wp_filter_group)
        cmd_options.use_relog_no_omp_spin(parser, wp_filter_group)
        cmd_options.use_relog_no_mpi_spin(parser, wp_filter_group)

        parser.add_option_group(wp_filter_group)

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
                "ERROR: Must use command line options to chose at least one phase to run.\n"
                "Use the option '--default_phases' to run the default phases. Use '--help' for more info.")
            util.CheckResult(-1, options, 'Checking command line options')

        # If user does not just want to delete all the files, then
        # read in configuration files and set global variables.
        #
        # import pdb;  pdb.set_trace()
        if not options.delete_all:
            self.Config.GetCfgGlobals(options,
                                      True)  # Yes, need 4 required parameters

            # Once the tracing configuration parameters are read, get the kit
            # in case pinplayhome was set on the command line.  Also, need to
            # reset the path in object 'self' and set the kit in self.phases.
            #
            # import pdb;  pdb.set_trace()
            kit_obj = self.GetKit()
            self.path = kit_obj.path
            self.script_path = kit_obj.path
            self.phases.SetKit(kit_obj)

        # If required, check to see if there any 'forbidden' char in some of
        # the parameters.
        #
        # import pdb;  pdb.set_trace()
        if self.kit_type == config.SDE and hasattr(options,
                                                   'spec') and not options.spec:
            self.Config.CheckForbiddenChar()

        # If doing code exclusion, check to make sure the file exists.
        #
        if options.relog_code_exclude:
            if not os.path.exists(options.relog_code_exclude):
                msg.PrintMsg(
                    'ERROR: The code exclusion file used for filtering does not exist:\n'
                    '    ' + options.relog_code_exclude)
                util.CheckResult(-1, options, 'Checking command line options')

        # Do some 'special' things on native Windows.
        #
        util.WindowsNativeCheck(options)

        return options

    def SetDefaults(self, options):
        """
        Set default values for variables which are not already defined.

        @param options Options given on cmd line

        @return No return value
        """

        # import pdb;  pdb.set_trace()
        if config.maxk == 0:
            config.maxk = 20
        if config.slice_size == 0:
            config.slice_size = 30000000
        if config.cutoff == 0.0:
            config.cutoff = 1.0

    def RunAdditionalPhases(self, wp_pb_dir, sim_replay_cmd, options):
        """
        There are no additional phases for PinPlay.  This is only a stub.

        @param wp_pb_dir     Directory containing whole program log files (pinballs)
        @param sim_replay_cmd Python script used to replay a pinball with a simulator
        @param options Options given on cmd line

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
        options = self.ParseCommandLine()

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

        # Create an new status file and zero out the file if it already exists.
        # If user sets parameter 'append_status', the old file is not deleted.
        #
        util.NewStatusFile(options)

        # Add 1500 to the warmup length in order to deal with problems that
        # occur when the start of a warmup region and the end of the previous
        # region are in the same basic block.
        #
        if not config.global_regions:
          config.warmup_length += 1500

        # Get variables used in many phases.
        #
        # import pdb;  pdb.set_trace()
        wp_pb_dir = util.GetWPDir()
        log_file_name = util.GetLogFile()

        # Print out the tracing configuration parameters.
        #
        if not options.list:
            self.phases.PrintTracingInfo(options, wp_pb_dir, log_file_name,
                                         self.PrintHome)

        #########################################################################
        #
        # Several phases which clean up tracing instances.
        #
        #########################################################################

        result = 0
        if options.delete or options.delete_wp:
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

        # If the user has given the name of the whole program pinball
        # directory, then save it. This must be done after the two delete
        # phases because these phases delete this configuration file.
        # Also, need to get default WP directory name in 'wp_pb_dir'.
        #
        # import pdb ; pdb.set_trace()
        if hasattr(options, 'whole_pgm_dir') and options.whole_pgm_dir:
            config.whole_pgm_dir = options.whole_pgm_dir
            self.Config.SaveCfgParameter('whole_pgm_dir', options.whole_pgm_dir)
            wp_pb_dir = util.GetWPDir()

        #########################################################################
        #
        # Run the phases of the script. They are in a specific order to ensure
        # all prerequisite phases are completed before a given phase is run.
        #
        #########################################################################

        # Run the application without pin/pintools.
        #
        if options.native_pure:
            util.PhaseBegin(options)
            result = self.phases.NativePure(options)
            util.CheckResult(
                result, options,
                'Pure native run %s' % config.PhaseStr(config.native_pure))

        # Run the application using pin, but no pintools.
        #
        if options.native_pin:
            util.PhaseBegin(options)
            result = self.phases.NativePin(self.logger_cmd, wp_pb_dir, log_file_name, \
                options)
            util.CheckResult(result, options, 'Native run with pin only %s' % \
                config.PhaseStr(config.native_pin))

        # Logging phase to generate initial whole program pinballs.
        #
        # import pdb;  pdb.set_trace()
        if options.log or options.default_phases:
            util.PhaseBegin(options)
            result = self.phases.Logger(self.logger_cmd, wp_pb_dir, log_file_name, \
                options)
            util.CheckResult(result, options, 'Whole program pinball generation %s' % \
                config.PhaseStr(config.log_whole))

        # All phases after this require the whole program pinball directory.  Exit with an
        # error if it does not exist.
        #
        # import pdb;  pdb.set_trace()
        if not os.path.isdir(wp_pb_dir) and not options.list and not options.debug and\
                not options.delete and not options.delete_all and not options.delete_wp and \
                not options.native_pure and not options.native_pin:
            string = '\nERROR: Can\'t proceed because the whole program pinball directory does not exist:\n' + \
                '      ' + wp_pb_dir + \
                '\nMust select at least one phase to run.  Try using option \'--default_phases\'.' + \
                '\nUse \'-h\' for help on selecting phases to run.'
            msg.PrintMsg(string)
            # Use -1 to force check to fail
            util.CheckResult(-1, options,
                             'Initial check to see if WP pinballs exist')

        # Print out the number of instructions in the whole program pinballs.
        #
        if not options.list and os.path.isdir(wp_pb_dir):
            msg.PrintMsg('')
            msg.PrintMsg('Initial whole program pinball(s)')
            util.PrintInstrCount(wp_pb_dir, options)

        #########################################################################
        #
        # Phases which relog whole program pinballs using a filter to remove
        # certain type of instructions (such as initialization or MPI spin).
        #
        # The relogging phases must be executed before the basic block vector
        # generation phase. This must be done in case one, or more, relogging
        # phases are executed.  If so then, the BB vector phase needs to use
        # the final relogged WP pinballs generated here.
        #
        #########################################################################

        # If the user gives one, or more, of the use_relog_* options, they want
        # to explictly define which filtered WP pinballs to use.  As a result,
        # unset the parameter 'relog_dir' just read from the per instance
        # tracing configuration file and from the config object.  This removes
        # the previous "sticky" value for the filtered WP pinballs.  Then use
        # the WP pinball directory before any filters have been applied.
        #
        # import pdb;  pdb.set_trace()
        if cmd_options.UseRelogOptionsSet(options):
            Config = config.ConfigClass()
            Config.ClearCfgParameter('relog_dir')
            config.relog_dir = ''
            wp_pb_dir = util.GetBaseWPDir()

        def FinalizeWPDir(wp_dir, string):
            """
            Set the filtered WP pinball directory to be the new default WP dir
            and print out the number of instructions in the newly filtered
            pinballs.
            """

            config.relog_dir = wp_dir
            if not options.debug and not options.list:
                # Only save this parameter if not just debugging or
                # listing the commands to be run.
                #
                self.Config.SaveCfgParameter('relog_dir', wp_dir)
            if not options.list and os.path.isdir(wp_dir):
                msg.PrintMsg('')
                msg.PrintMsg(string)
                util.PrintInstrCount(wp_dir, options)

            return

        # The format of the code for each relogging (filtering) phase contains
        # 3 components:
        #
        #   1) If going to use relogged WP pinballs for the phase (indicated by
        #      either 'relog_*' or 'use_relog_*' options), then get the
        #      appropriate WP directory name for this phase.  Need to always do
        #      this, even when not relogging, because a previous run generated
        #      relogged WP pinballs.
        #
        #   2) If going to filter with relogging (indicated by a 'relog_*'
        #      option), then run the relogging phase.
        #
        #   3) Set the name of the default whole program pinball directory to
        #      the directory with the relogged WP pinballs.

        # Relog with a user defined name for the relogged directory, and hopefully a set of
        # knobs to define what action to take when relogging.
        #
        # import pdb;  pdb.set_trace()
        if options.use_relog_name != '' or options.relog_name:
            relog_wp_dir = util.GetRelogPhaseDir(wp_pb_dir, config.RELOG_NAME,
                                                 options)
            if options.relog_name:
                if not options.list:
                    msg.PrintMsgDate('Filtering whole program pinballs with user defined name: %s %s' % \
                        (options.relog_name, config.PhaseStr(config.filter_user_defn)))
                util.PhaseBegin(options)
                result = self.phases.RelogWholeName(self.replay_cmd, wp_pb_dir,
                                                    relog_wp_dir, options)
                if not options.list:
                    msg.PrintMsgDate('Finished filtering whole program pinballs with user defined name: %s %s' % \
                        (options.relog_name, config.PhaseStr(config.filter_user_defn)))
                util.CheckResult(result, options, 'Filtering WP pinballs with user defined name: %s %s' % \
                    (options.relog_name, config.PhaseStr(config.filter_user_defn)))

            # No errors, commit to using the new pinballs.
            #
            wp_pb_dir = relog_wp_dir
            FinalizeWPDir(
                relog_wp_dir,
                'Whole program pinball(s) filtered with user defined name: ' +
                options.relog_name)

        # Relog with a focus thread.
        #
        if options.use_relog_focus or options.relog_focus:
            relog_wp_dir = util.GetRelogPhaseDir(wp_pb_dir, config.RELOG_FOCUS,
                                                 options)
            if options.relog_focus:
                if not options.list:
                    msg.PrintMsgDate('Filtering whole program pinballs with a focus thread %s' % \
                        config.PhaseStr(config.filter_focus_thread))
                # import pdb;  pdb.set_trace()
                util.PhaseBegin(options)
                result = self.phases.RelogWholeFocus(self.replay_cmd,
                                                     wp_pb_dir, relog_wp_dir,
                                                     options)
                if not options.list:
                    msg.PrintMsgDate('Finished filtering whole program pinballs with a focus thread %s' % \
                        config.PhaseStr(config.filter_focus_thread))
                util.CheckResult(result, options, 'Filtering WP pinballs with focus thread %s' % \
                    config.PhaseStr(config.filter_focus_thread))

            # No errors, commit to using the new pinballs.
            #
            wp_pb_dir = relog_wp_dir
            FinalizeWPDir(
                relog_wp_dir,
                'Whole program pinball(s) filtered with a focus thread')

            # If pinballs were relogged with a focus thread, then in the
            # remaining phases the focus thread must be 0.  Relogging generates
            # per thread whole program pinballs (which only have thread 0).  To
            # enforce this, we change the focus_thread in the config object to
            # be 0.
            #
            config.focus_thread = 0

            # Also need to set options.use_relog_focus = True because we are will be using
            # WP pinballs which have been relogged with a focus thread.
            #
            options.use_relog_focus = True

        # Relog to remove initialization instructions.  Do this before removing cleanup or
        # MPI spin instructions.
        #
        # import pdb;  pdb.set_trace()
        if options.use_relog_no_init or options.relog_no_init:
            relog_wp_dir = util.GetRelogPhaseDir(wp_pb_dir,
                                                 config.RELOG_NO_INIT, options)
            if options.relog_no_init:
                if not options.list:
                    msg.PrintMsgDate('Filtering whole program pinballs to remove initialization instructions %s' % \
                        config.PhaseStr(config.filter_init))
                util.PhaseBegin(options)
                result = self.phases.RelogWholeRemoveInit(self.replay_cmd,
                                                          wp_pb_dir,
                                                          relog_wp_dir, options)
                if not options.list:
                    msg.PrintMsgDate('Finished filtering whole program pinballs to remove initialization instructions %s' % \
                        config.PhaseStr(config.filter_init))
                util.CheckResult(result, options, 'Filtering WP pinballs to remove init instructions %s' % \
                    config.PhaseStr(config.filter_init))

            # No errors, commit to using the new pinballs.
            #
            wp_pb_dir = relog_wp_dir
            FinalizeWPDir(
                relog_wp_dir,
                'Whole program pinball(s) filtered to remove initialization instructions')

        # Relog to remove cleanup instructions.  Do this before removing MPI spin
        # instructions.
        #
        # import pdb;  pdb.set_trace()
        if options.use_relog_no_cleanup or options.relog_no_cleanup:
            relog_wp_dir = util.GetRelogPhaseDir(wp_pb_dir,
                                                 config.RELOG_NO_CLEANUP,
                                                 options)
            if options.relog_no_cleanup:
                if not options.list:
                    msg.PrintMsgDate('Filtering whole program pinballs to remove cleanup instructions %s' % \
                        config.PhaseStr(config.filter_cleanup))
                util.PhaseBegin(options)
                result = self.phases.RelogWholeRemoveCleanup(
                    self.replay_cmd, wp_pb_dir, relog_wp_dir, options)
                if not options.list:
                    msg.PrintMsgDate('Finished filtering whole program pinballs to remove cleanup instructions %s' % \
                        config.PhaseStr(config.filter_cleanup))
                util.CheckResult(result, options, 'Filtering WP pinballs to remove cleanup instructions %s' % \
                    config.PhaseStr(config.filter_cleanup))

            # No errors, commit to using the new pinballs.
            #
            wp_pb_dir = relog_wp_dir
            FinalizeWPDir(
                relog_wp_dir,
                'Whole program pinball(s) filtered to remove cleanup instructions')

        # Relog to exclude code (instructions) between two addresses. Do this
        # before removing MPI spin instructions.
        #
        # import pdb;  pdb.set_trace()
        if options.use_relog_code_exclude != '' or options.relog_code_exclude:
            relog_wp_dir = util.GetRelogPhaseDir(wp_pb_dir,
                                                 config.RELOG_CODE_EXCLUDE,
                                                 options)
            if options.relog_code_exclude:
                if not options.list:
                    msg.PrintMsgDate('Filtering whole program pinballs with code exclusion %s' % \
                        config.PhaseStr(config.filter_code_exclude))
                util.PhaseBegin(options)
                result = self.phases.RelogWholeCodeExclude(
                    self.replay_cmd, wp_pb_dir, relog_wp_dir, options)
                if not options.list:
                    msg.PrintMsgDate('Finished filtering whole program pinballs with code exclusion %s' % \
                        config.PhaseStr(config.filter_code_exclude))
                util.CheckResult(result, options, 'Filtering WP pinballs with code exclusion %s' % \
                    config.PhaseStr(config.filter_code_exclude))

            # No errors, commit to using the new pinballs.
            #
            wp_pb_dir = relog_wp_dir
            FinalizeWPDir(
                relog_wp_dir,
                'Whole program pinball(s) filtered with code exclusion')

        # Relog to remove OpenMP spin instructions.
        #
        # import pdb;  pdb.set_trace()
        if options.use_relog_no_omp_spin or options.relog_no_omp_spin:
            relog_wp_dir = util.GetRelogPhaseDir(wp_pb_dir,
                                                 config.RELOG_NO_OMP_SPIN,
                                                 options)
            if options.relog_no_omp_spin:
                if not options.list:
                    msg.PrintMsgDate('Filtering whole program pinballs to remove OpenMP spin instructions %s' % \
                        config.PhaseStr(config.filter_OMP_spin))
                util.PhaseBegin(options)
                result = self.phases.RelogWholeRemoveOMPSpin(
                    self.replay_cmd, wp_pb_dir, relog_wp_dir, options)
                if not options.list:
                    msg.PrintMsgDate('Finished filtering whole program pinballs to remove OpenMP spin instructions %s' % \
                        config.PhaseStr(config.filter_OMP_spin))
                util.CheckResult(result, options, 'Filtering WP pinballs to remove OpenMP spin instructions %s' % \
                    config.PhaseStr(config.filter_OMP_spin))

            # No errors, commit to using the new pinballs.
            #
            wp_pb_dir = relog_wp_dir
            FinalizeWPDir(
                relog_wp_dir,
                'Whole program pinball(s) filtered to remove OpenMP spin instructions')

        # Relog to remove MPI spin instructions.
        #
        # import pdb;  pdb.set_trace()
        if options.use_relog_no_mpi_spin or options.relog_no_mpi_spin:
            relog_wp_dir = util.GetRelogPhaseDir(wp_pb_dir,
                                                 config.RELOG_NO_MPI_SPIN,
                                                 options)
            if options.relog_no_mpi_spin:
                if not options.list:
                    msg.PrintMsgDate('Filtering whole program pinballs to remove MPI spin instructions %s' % \
                        config.PhaseStr(config.filter_MPI_spin))
                util.PhaseBegin(options)
                result = self.phases.RelogWholeRemoveMPISpin(
                    self.replay_cmd, wp_pb_dir, relog_wp_dir, options)
                if not options.list:
                    msg.PrintMsgDate('Finished filtering whole program pinballs to remove MPI spin instructions %s' % \
                        config.PhaseStr(config.filter_MPI_spin))
                util.CheckResult(result, options, 'Filtering WP pinballs to remove MPI spin instructions %s' % \
                    config.PhaseStr(config.filter_MPI_spin))

            # No errors, commit to using the new pinballs.
            #
            wp_pb_dir = relog_wp_dir
            FinalizeWPDir(
                relog_wp_dir,
                'Whole program pinball(s) filtered to remove MPI spin instructions')

        if not options.list:
            msg.PrintMsgPlus(
                'Using whole program pinballs in dir: ' + wp_pb_dir)
            if (cmd_options.UseRelogOptionsSet(options) or \
               cmd_options.RelogOptionsSet(options)) and \
               os.path.isdir(wp_pb_dir):
                msg.PrintMsg('')
                util.PrintInstrCount(wp_pb_dir, options)

        #########################################################################
        #
        # These phases are run with the whole progam pinballs defined/generated
        # in the previous phases.
        #
        #########################################################################

        # Make sure any relogged whole program pinball directory exist.  Exit with an
        # error if it does not exist.
        #
        # import pdb;  pdb.set_trace()
        if not os.path.isdir(wp_pb_dir) and not options.list and not options.debug and\
                not options.delete and not options.delete_all and not options.delete_wp and \
                not options.native_pure and not options.native_pin:
            string = 'ERROR: Can\'t proceed because the whole program pinball directory does not exist:\n' + \
                '      ' + wp_pb_dir
            msg.PrintMsg(string)
            # Use -1 to force check to fail
            util.CheckResult(-1, options,
                             'Second check to see if WP pinballs exist')

        # Do not run replay whole program pinballs as one of the default
        # phases.  The user must explicitly include this option.  This is to
        # save time during the tracing process.
        #
        if options.replay:
            if not options.list:
                msg.PrintMsgDate('Replaying all whole program pinballs %s' % \
                    config.PhaseStr(config.replay_whole))
            util.PhaseBegin(options)
            result = self.phases.Replay(self.replay_cmd, wp_pb_dir, options)
            if not options.list:
                msg.PrintMsgDate('Finished replaying all whole program pinballs %s' % \
                    config.PhaseStr(config.replay_whole))
            util.CheckResult(result, options, 'Replay of whole program pinballs %s' % \
                config.PhaseStr(config.replay_whole))

        # Generate basic block vectors.
        #
        if options.basic_block_vector or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Generating basic block vectors %s' % \
                    config.PhaseStr(config.gen_BBV))
            util.PhaseBegin(options)
            result = self.phases.BasicBlockVector(self.replay_cmd, wp_pb_dir,
                                                  options)
            if result == 0:
                result = util.WaitJobs(options)
            if not options.list:
                msg.PrintMsgDate('Finished basic block vector generation %s' % \
                    config.PhaseStr(config.gen_BBV))
            util.CheckResult(result, options, 'Basic block vector generation %s' % \
                config.PhaseStr(config.gen_BBV))

        # Run Simpoints to genenerate representative regions.
        #
        if options.simpoint or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Running Simpoint on all processes %s' % \
                    config.PhaseStr(config.Simpoint))
            # Setup dictionary of parameters for method RunAllDir()
            #
            param = {'options': options}
            util.PhaseBegin(options)
            result = util.RunAllDir(wp_pb_dir, self.phases.RunSimPoint, True,
                                    param)
            if not options.list:
                msg.PrintMsgDate('Finished running Simpoint for all processes %s' % \
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
            result = self.phases.MultiIterGenRegionPinballs(wp_pb_dir,
                                                            self.replayer_cmd,
                                                            options)
            if result == 0:
                if not options.list:
                    msg.PrintMsgPlus(
                        'Waiting on final concurrent region pinball generation')
                result = util.WaitJobs(options)
            if not options.list:
                msg.PrintMsgDate('Finished generating region pinballs %s' % \
                    config.PhaseStr(config.relog_regions))
            util.CheckResult(result, options, 'Region pinball generation %s' %\
                config.PhaseStr(config.relog_regions))

        # Do not run replay region pinballs as one of the default phases.  The
        # user must explicitly include this option.  This is to save time
        # during the tracing process.
        #
        if options.replay_region:
            result = 0
            if not options.list:
                msg.PrintMsgDate('Replaying all region pinballs %s' % \
                    config.PhaseStr(config.replay_regions))
            # import pdb;  pdb.set_trace()
            util.PhaseBegin(options)
            for pp_dir in util.GetRegionPinballDir():
                # Accumulate any errors which occur, but don't check for errors
                # until all the pinballs have been replayed.
                #
                r = self.phases.Replay(self.replay_cmd, pp_dir, options)
                result = result or r
            if not options.list:
                msg.PrintMsgDate('Finished replaying all region pinballs %s' % \
                    config.PhaseStr(config.replay_regions))
            util.CheckResult(result, options, 'Replay of region pinballs %s' % \
                config.PhaseStr(config.replay_regions))
            result = 0  # Remove the return values from replaying region pinballs

        # If there are any additional phases, then run them.
        #
        self.RunAdditionalPhases(wp_pb_dir, self.sim_replay_cmd, options)

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

    pp = PinPoints()
    result = pp.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    if (result > 0):
        sys.exit(result)
