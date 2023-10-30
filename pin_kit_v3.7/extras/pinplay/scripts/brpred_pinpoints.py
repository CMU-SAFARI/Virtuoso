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
# $Id: brpred_pinpoints.py,v 1.23 2015/08/15 19:58:48 tmstall Exp tmstall $

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
import kit
import brpred_kit
import phases
import util


class BrPredPinPoints(pinpoints.PinPoints):

    # Use the PinPlay scripts for logging/replay.
    #
    logger_cmd = 'logger.py'
    replay_cmd = 'replay_dir.py'
    replayer_cmd = 'replayer.py'

    # Simulator replay script
    #
    sim_replay_cmd = 'brpred_replay_dir.py'

    # Local simulator object.
    #
    sim_kit = None

    def GetKit(self):
        """ Get the PinPlay kit. """

        kit_obj = kit.Kit()
        self.pin = kit_obj.pin
        self.path = kit_obj.path
        self.kit_type = kit_obj.kit_type
        return kit_obj

    def GetSimKit(self):
        """ Get the simulator kit. """

        kit_obj = brpred_kit.BrPredKit()
        self.pin = kit_obj.pin
        self.path = kit_obj.path
        self.kit_type = kit_obj.kit_type
        return kit_obj

    ###################################################################################
    #
    # Add additional options not in PinPlay
    #
    ###################################################################################

    def AddAdditionalPhaseOptions(self, parser, phase_group):
        """Add additional phase options which the simulator needs, but PinPlay doesn't use."""

        cmd_options.region_sim(parser, phase_group)
        cmd_options.whole_sim(parser, phase_group)
        cmd_options.pred_error(parser, phase_group)

        return

    ###################################################################################
    #
    # Extra phases run for simulator
    #
    ###################################################################################

    def RunAdditionalPhases(self, wp_pb_dir, sim_replay_cmd, options):
        """
        Run the additional phases for the simulator which are not run as
        part of the usual PinPlay phases.
        """

        phases_obj = phases.Phases()
        self.sim_kit = self.GetSimKit()

        # Run branch predictor simulator on region pinballs.
        #
        if options.region_sim or options.default_phases:
            # Print out simulator results every warmup_length instructions.
            #
            for pp_dir in util.GetRegionPinballDir():
                phase_length = options.warmup_length
                if phase_length == 0:
                    phase_length = options.slice_size

                if not options.list:
                    msg.PrintMsgDate('Running simulator on region pinballs: %s' % \
                        config.PhaseStr(config.sim_regions))
                util.PhaseBegin(options)
                result = self.sim_kit.RunSimulator(pp_dir, sim_replay_cmd,
                                                   phase_length, options)
                if not options.list:
                    msg.PrintMsgDate('Finished running simulator on region pinballs: %s' % \
                        config.PhaseStr(config.sim_regions))
                util.CheckResult(result, options, 'simulator on region pinballs: %s' % \
                    config.PhaseStr(config.sim_regions))

        # Run branch predictor simulator on whole program pinballs.
        #
        if options.whole_sim or options.default_phases:
            # Set phase_length to print out simulator results every slice_size instructions.
            #
            phase_length = options.slice_size

            if not options.list:
                msg.PrintMsgDate('Running simulator on whole program pinballs: %s' % \
                    config.PhaseStr(config.sim_whole))
            util.PhaseBegin(options)
            result = self.sim_kit.RunSimulator(wp_pb_dir, sim_replay_cmd,
                                               phase_length, options)
            if not options.list:
                msg.PrintMsgDate('Finished running simulator on whole program pinballs: %s' % \
                    config.PhaseStr(config.sim_whole))
            util.CheckResult(result, options, 'simulator on whole program pinballs: %s' % \
                config.PhaseStr(config.sim_whole))

        # Calculate prediction error.
        #
        if options.pred_error or options.default_phases:
            if hasattr(options, 'pccount_regions') and options.pccount_regions:
                msg.PrintMsg(
                    '\n Prediction with PCregions is NIY')
                return 0
            if not options.list:
                msg.PrintMsgDate('Calculating prediction error: %s' % \
                    config.PhaseStr(config.pred_error))
            util.PhaseBegin(options)
            result = phases_obj.CalcPredError(wp_pb_dir, self.sim_kit, options)
            if not options.list:
                msg.PrintMsgDate('Finished calculating prediction error: %s' % \
                    config.PhaseStr(config.pred_error))
            util.CheckResult(result, options, 'Prediction error calculation: %s' % \
                config.PhaseStr(config.pred_error))
        # Assume nothing has gone wrong at this point.
        #
        return 0


def main():
    """ Process command line arguments and run the script """

    pp = BrPredPinPoints()
    result = pp.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
