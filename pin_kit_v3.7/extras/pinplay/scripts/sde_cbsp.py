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
# $Id: sde_cbsp.py,v 1.11 2015/08/11 18:00:13 tmstall Exp tmstall $

# This is a script to generate traces using the CBSP method (Cross Binary SimPoint)
#

import sys

# Local modules
#
import cbsp
import config
import cmd_options
import msg
import phases
import sde_phases
import sde_pinpoints
import util


class SDECBSP(sde_pinpoints.SDEPinPoints, cbsp.CBSP):
    """
    Top level script for generating traces using Cross Binary Simpoints with SDE.

    SDEPinPoints needs to be first in the SDECBSP class definition.  This is
    required so methods/data from SDEPinPoints (which uses a SDE kit) will override
    PinPlay methods/data used in class CBSP (which uses a Pin kit).

    """

    def default_phases(self, parser, group):
        """
        For SDE CBSP, redefine the option '--default_phases' instead
        of using the one defined in cbsp.default_phases().

        @param parser Optparse object
        @param group Parser option group in which options are displayed

        @return No return value
        """

        method = cmd_options.GetMethod(parser, group)
        method("--default_phases",
               dest="default_phases",
               action="store_true",
               help="Always run these default phases: log, cb_match, simpoint, "
               "region_pinball, lit_gen, region_sim, whole_sim and pred_error.")

    def AddAdditionalOptions(self, parser):
        """
        Add additional default options which SDE needs, but PinPlay doesn't use.

        @param parser Optparse object

        @return No return value
        """

        cmd_options.lit_options(parser)

    def AddAdditionalPhaseOptions(self, parser, phase_group):
        """
        Add additional phase options CBSP which SDE needs, but PinPlay doesn't use.

        @param parser      Optparse object
        @param phase_group Phase parser group where options are located

        @return No return value
        """

        cmd_options.lit_gen(parser, phase_group)
        cmd_options.region_sim(parser, phase_group)
        cmd_options.whole_sim(parser, phase_group)
        cmd_options.pred_error(parser, phase_group)

    def RunAdditionalPhases(self, sim_replay_cmd, options, bin_options):
        """
        Run the CBSP additional phases for SDE which are not run for PinPlay.

        @param sim_replay_cmd Script which run the simulator on pinballs
        @param options Options given on cmd line
        @param options Options for each CBSP binary

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

        # Generate LMAT/LIT files
        #
        result = 0
        if options.lit_gen or options.default_phases:
            if not options.list:
                msg.PrintMsgDate('Generating traces for region pinballs %s' % \
                    config.PhaseStr(config.LIT))
            util.PhaseBegin(options)
            for bopts in bin_options:
                wp_dir = util.GetDefaultWPDir(bopts)
                log_file_name = util.GetLogFile(bopts)
                self.Config.SetPerBinParams(bopts)
                result = s_phases.GenAllLitFiles(self.replayer_cmd, bopts)
                if result == 0:
                    if not options.list:
                        msg.PrintMsgPlus('Waiting on final trace generation')
                    result = util.WaitJobs(options)
                util.CheckResult(result, options, 'Traces for: %s %s' % \
                    (log_file_name, config.PhaseStr(config.LIT)), intermediate_phase=True)
            if not options.list:
                msg.PrintMsgDate('Finished generating traces for region pinballs %s' % \
                    config.PhaseStr(config.LIT))
            util.CheckResult(result, options, 'Trace file generation %s' %\
                config.PhaseStr(config.LIT))

        # Run CMPSim simulator on region pinballs.
        #
        result = 0
        if options.region_sim or options.default_phases:
            # Print out CMPSim results every warmup_length instructions.
            #
            phase_length = options.warmup_length / config.instr_cmpsim_phase
            if phase_length == 0:
                phase_length = 1
            for bopts in bin_options:
                self.Config.SetPerBinParams(bopts)
                for pp_dir in util.GetRegionPinballDir(bopts):
                    if not options.list:
                        msg.PrintMsgDate('Running CMPSim on region pinballs in dir: %s %s' % \
                            (pp_dir, config.PhaseStr(config.CMPsim_regions)))
                    util.PhaseBegin(options)
                    result = sim_kit.RunSimulator(pp_dir, sim_replay_cmd,
                                                  phase_length, bopts)
                    util.CheckResult(result, options, 'CMPSim for: %s %s' % \
                        (pp_dir, config.PhaseStr(config.CMPsim_regions)), intermediate_phase=True)
            if not options.list:
                msg.PrintMsgDate('Finished running CMPSim on region pinballs in dir %s %s' % \
                    (pp_dir, config.PhaseStr(config.CMPsim_regions)))
            util.CheckResult(result, options, 'CMPSim on region pinballs: %s' % \
                        config.PhaseStr(config.CMPsim_regions))

        # Run CMPSim simulator on whole program pinballs.
        #
        if options.whole_sim or options.default_phases:
            # Set phase_length to print out CMPSim results every slice_size instructions.
            #
            phase_length = options.slice_size / config.instr_cmpsim_phase
            if phase_length == 0:
                phase_length = 1
            for bopts in bin_options:
                if not options.list:
                    msg.PrintMsgDate('Running CMPSim on whole program pinballs %s' % \
                        config.PhaseStr(config.CMPsim_whole))
                util.PhaseBegin(options)
                wp_dir = util.GetDefaultWPDir(bopts)
                self.Config.SetPerBinParams(bopts)
                result = sim_kit.RunSimulator(wp_dir, sim_replay_cmd,
                                              phase_length, bopts)
                util.CheckResult(result, options, 'CMPSim for: %s %s' % \
                    (wp_dir, config.PhaseStr(config.CMPsim_whole)), intermediate_phase=True)
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
            for bopts in bin_options:
                wp_dir = util.GetDefaultWPDir(bopts)
                result = s_phases.CalcPredError(wp_dir, sim_kit, bopts)
                util.CheckResult(result, options, 'Prediction error for: %s %s' % \
                    (wp_dir, config.PhaseStr(config.pred_error)), intermediate_phase=True)
            if not options.list:
                msg.PrintMsgDate('Finished calculating prediction error %s' % \
                    config.PhaseStr(config.pred_error))
            util.CheckResult(result, options, 'Prediction error calculation %s' % \
                config.PhaseStr(config.pred_error))

        # Assume nothing has gone wrong at this point.
        #
        return result


def main():
    """
    Process command line arguments and run the script

    @return Exit code from running the script
    """

    cb = SDECBSP()
    result = cb.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    if (result > 0):
        sys.exit(result)
