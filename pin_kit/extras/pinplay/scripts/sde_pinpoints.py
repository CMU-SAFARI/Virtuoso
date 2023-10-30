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
# $Id: sde_pinpoints.py,v 1.53 2015/08/15 19:58:48 tmstall Exp tmstall $

# This is a script to replay one pinplay process
#

import sys
import os

# Local modules
#
import cmd_options
import config
import msg
import pinpoints
import sde_kit
import sde_cmpsim_kit
import sde_phases
import phases
import util


class SDEPinPoints(pinpoints.PinPoints):
    """
    Top level script for tracing with SDE/PinPlay derived from class PinPoints.
    """

    logger_cmd = 'sde_logger.py'
    replay_cmd = 'sde_replay_dir.py'
    replayer_cmd = 'sde_replayer.py'
    sim_replay_cmd = 'sde_cmpsim_replay_dir.py'
    sim_run_cmd = 'sim_run_dir.py'

    def PrintHome(self, options):
        """
        Print the directory where the SDE kit is located.

        @return No return value
        """

        # If this variable is a symbolic link, then print the real directory name.
        #
        if os.path.islink(self.path):
            path = os.path.realpath(self.path)
        else:
            path = self.path
        msg.PrintMsg('Sdehome:                   ' + path)

    def KitOption(self, parser, param_group):
        """
        Add the SDE kit command line option to the current list of options.

        @return No return value
        """

        cmd_options.sdehome(parser, param_group)

    def GetKit(self):
        """
        Get the SDE kit.

        @return SDE kit
        """

        kit_obj = sde_kit.SDEKit()
        self.pin = kit_obj.pin
        self.path = kit_obj.path
        self.script_path = kit_obj.path
        self.kit_type = kit_obj.kit_type

        return kit_obj

    def GetSimKit(self):
        """
        Get the simulator kit.

        @return Simulator kit
        """

        kit_obj = sde_cmpsim_kit.CMPsimKit()
        self.pin = kit_obj.pin
        self.path = kit_obj.path
        self.kit_type = kit_obj.kit_type

        return kit_obj

    ###################################################################################
    #
    # Run phases only in SDE, not PinPlay
    #
    ###################################################################################

    def AddAdditionalOptions(self, parser):
        """
        Add additional default options which SDE needs, but PinPlay doesn't use.

        @param parser Optparse object

        @return No return value
        """

        cmd_options.lit_options(parser)

    def AddAdditionalPhaseOptions(self, parser, phase_group):
        """
        Add additional phase options which SDE needs, but PinPlay doesn't use.

        @param parser      Optparse object
        @param phase_group Phase parser group where options are located

        @return No return value
        """

        cmd_options.lit_gen(parser, phase_group)
        cmd_options.traceinfo(parser, phase_group)
        cmd_options.region_sim(parser, phase_group)
        cmd_options.whole_sim(parser, phase_group)
        cmd_options.pred_error(parser, phase_group)
        cmd_options.verify(parser, phase_group)
        cmd_options.imix_lit(parser, phase_group)
        cmd_options.imix_region(parser, phase_group)
        cmd_options.imix_whole(parser, phase_group)

    def AddAdditionalModifyOptions(self, parser, modify_group):
        """
        Add additional modify options which SDE needs, but PinPlay doesn't use.

        @param parser      Optparse object
        @param phase_group Modify parser group where options are located

        @return No return value
        """

        cmd_options.coop_lit(parser, modify_group)
        cmd_options.spec(parser, modify_group)

    def AddVerifyOptions(self, parser):
        """
        Add the options which used for LIT file verification.

        @param parser Optparse object

        @return No return value
        """

        # Top level command line options which only apply to the LIT file verification phase.
        #
        verify_LIT_group = cmd_options.VerifyLITPhaseGroup(parser)

        # cmd_options.archsim_cf_file(parser, verify_LIT_group)
        cmd_options.archsim_config_dir(parser, verify_LIT_group)
        cmd_options.simhome(parser, verify_LIT_group)
        cmd_options.sim_options(parser, verify_LIT_group)
        cmd_options.processor(parser, verify_LIT_group)

        parser.add_option_group(verify_LIT_group)

    def RunAdditionalPhases(self, wp_pb_dir, sim_replay_cmd, options):
        """
        Run the additional phases for SDE which are not run for PinPlay.

        @param wp_pb_dir     Directory containing whole program log files (pinballs)
        @param sim_replay_cmd Python script used to replay a pinball with a simulator
        @param options Options given on cmd line

        @return Exit code from last phase executed
        """

        # The SDE phases object needs an SDE kit object.
        #
        s_phases = sde_phases.SDEPhases()
        kit_obj = self.GetKit()
        s_phases.SetKit(kit_obj)

        # Need the appropriate simulator kit too.
        #
        sim_kit = self.GetSimKit()

        # Generate LMAT/LIT files.
        #
        result = 0
        if options.lit_gen or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Generating traces for region pinballs %s' % \
                    config.PhaseStr(config.LIT))
            util.PhaseBegin(options)
            result = s_phases.GenAllLitFiles(self.replayer_cmd, options)
            #if result == 0:
            # if not options.list:
            #    msg.PrintMsgPlus('Waiting on final concurrent region pinball generation')
            # result = util.WaitJobs(options)
            if not options.list:
                msg.PrintMsgDate('Finished generating traces for region pinballs %s' % \
                    config.PhaseStr(config.LIT))
            util.CheckResult(result, options, 'Trace file generation %s' % \
                config.PhaseStr(config.LIT))

        # Generate traceinfo files.
        #
        if options.traceinfo or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Generating traceinfo files %s' % \
                    config.PhaseStr(config.traceinfo))
            util.PhaseBegin(options)
            result = s_phases.GenTraceinfoFiles(options)
            if not options.list:
                msg.PrintMsgDate('Finished generating traceinfo files %s' % \
                    config.PhaseStr(config.traceinfo))
            util.CheckResult(result, options, 'Traceinfo file generation %s' % \
                config.PhaseStr(config.traceinfo))

        # Run CMPSim simulator on region pinballs.
        #
        if options.region_sim or options.default_phases:
            # Print out CMPSim results every warmup_length instructions.
            #
            phase_length = options.warmup_length / config.instr_cmpsim_phase
            if phase_length == 0:
                phase_length = 1
            for pp_dir in util.GetRegionPinballDir():
                if not options.list:
                    msg.PrintMsgDate('Running CMPSim on region pinballs in dir: %s %s' % \
                        (pp_dir, config.PhaseStr(config.CMPsim_regions)))
                util.PhaseBegin(options)
                result = sim_kit.RunSimulator(pp_dir, sim_replay_cmd,
                                              phase_length, options)
                if not options.list:
                    msg.PrintMsgDate('Finished running CMPSim on region pinballs in dir %s %s' % \
                        (pp_dir, config.PhaseStr(config.CMPsim_regions)))
                util.CheckResult(result, options, 'CMPSim on pinballs, dir: %s %s' % \
                        (pp_dir, config.PhaseStr(config.CMPsim_regions)))

        # Run CMPSim simulator on whole program pinballs.
        #
        if options.whole_sim or options.default_phases:
            # Set phase_length to print out CMPSim results every slice_size instructions.
            #
            phase_length = options.slice_size / config.instr_cmpsim_phase
            if phase_length == 0:
                phase_length = 1
            if not options.list:
                msg.PrintMsgDate('Running CMPSim on whole program pinballs %s' % \
                    config.PhaseStr(config.CMPsim_whole))
            util.PhaseBegin(options)
            result = sim_kit.RunSimulator(wp_pb_dir, sim_replay_cmd,
                                          phase_length, options)
            if not options.list:
                msg.PrintMsgDate('Finished running CMPSim on whole program pinballs %s' % \
                    config.PhaseStr(config.CMPsim_whole))
            util.CheckResult(result, options, 'CMPSim on whole program pinballs %s' % \
                config.PhaseStr(config.CMPsim_whole))

        # Calculate prediction error from simulator data files.
        #
        if options.pred_error or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Calculating prediction error %s' % \
                    config.PhaseStr(config.pred_error))
            util.PhaseBegin(options)
            result = s_phases.CalcPredError(wp_pb_dir, sim_kit, options)
            if not options.list:
                msg.PrintMsgDate('Finished calculating prediction error %s' % \
                    config.PhaseStr(config.pred_error))
            util.CheckResult(result, options, 'Prediction error calculation %s' % \
                config.PhaseStr(config.pred_error))

        # Verify the LIT files with the simulator.
        #
        if options.verify:
            if not options.list:
                msg.PrintMsgDate('Verifying LIT files %s' % \
                    config.PhaseStr(config.verify_LIT))
            util.PhaseBegin(options)
            result = s_phases.VerifyLITFiles(self.sim_run_cmd, options)
            if not options.list:
                msg.PrintMsgDate('Finished verifying LIT files %s' % \
                    config.PhaseStr(config.verify_LIT))
            util.CheckResult(result, options, 'LIT file verification %s' % \
                config.PhaseStr(config.verify_LIT))

        # Generate instruction mix for the whole program pinballs.
        #
        if options.imix_whole:
            if not options.list:
                msg.PrintMsgDate('Generating imix on whole program pinballs  %s' % \
                    config.PhaseStr(config.imix_whole))
            # Setup dictionary of parameters for method RunAllDir()
            #
            param = {'options': options, 'replayer_cmd': self.replayer_cmd}
            util.PhaseBegin(options)
            result = util.RunAllDir(wp_pb_dir, s_phases.GenImix, True, param)
            if result == 0:
                if not options.list:
                    msg.PrintMsgPlus(
                        'Waiting on final whole program pinball imix generation')
                result = util.WaitJobs(options)
            if not options.list:
                msg.PrintMsgDate('Finished generating imix on whole program pinballs %s' % \
                    config.PhaseStr(config.imix_whole))
            util.CheckResult(result, options, 'Imix on whole program pinballs %s' % \
                config.PhaseStr(config.imix_whole))

        # Generate instruction mix for the LIT files.
        #
        if options.imix_lit:
            if not options.list:
                msg.PrintMsgDate('Generating imix on LIT files  %s' % \
                    config.PhaseStr(config.imix_lit))
            # Setup dictionary of parameters for method RunAllDir()
            #
            param = {'options': options, 'replayer_cmd': self.replayer_cmd}
            util.PhaseBegin(options)
            for lit_dir in util.GetLitDir():
                result = util.RunAllDir(lit_dir, s_phases.GenImix, True, param)
            if result == 0:
                if not options.list:
                    msg.PrintMsgPlus('Waiting on final LIT files generation')
                result = util.WaitJobs(options)
            if not options.list:
                msg.PrintMsgDate('Finished generating imix on LIT files %s' % \
                    config.PhaseStr(config.imix_lit))
            util.CheckResult(result, options, 'Imix on LIT files %s' % \
                config.PhaseStr(config.imix_lit))

        # Generate instruction mix for the region pinballs.
        #
        if options.imix_region:
            if not options.list:
                msg.PrintMsgDate('Generating imix on region pinballs  %s' % \
                    config.PhaseStr(config.imix_regions))
            # Setup dictionary of parameters for method RunAllDir()
            #
            param = {'options': options, 'replayer_cmd': self.replayer_cmd}
            util.PhaseBegin(options)
            for pp_dir in util.GetRegionPinballDir():
                result = util.RunAllDir(pp_dir, s_phases.GenImix, True, param)
            if result == 0:
                if not options.list:
                    msg.PrintMsgPlus(
                        'Waiting on final region pinballs generation')
                result = util.WaitJobs(options)
            if not options.list:
                msg.PrintMsgDate('Finished generating imix on region pinballs %s' % \
                    config.PhaseStr(config.imix_regions))
            util.CheckResult(result, options, 'Imix on region pinballs %s' % \
                config.PhaseStr(config.imix_regions))

        # Assume nothing has gone wrong at this point.
        #
        return result


def main():
    """
    Process command line arguments and run the script

    @return Exit code from running the script
    """

    pp = SDEPinPoints()
    result = pp.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
