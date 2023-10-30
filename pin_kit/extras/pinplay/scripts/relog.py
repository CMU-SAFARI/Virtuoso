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
# $Id: relog.py,v 1.10 2015/06/12 22:20:26 tmstall Exp tmstall $

import sys
import os
import subprocess
import optparse
import time

# Local modules
#
import cmd_options
import config
import replay
import msg
import util


class Relog(replay.Replay):
    """
    Replay DrDebug log files and generate a new log file, based on options
    used to define the new file.
    """

    def GetHelpParams(self):
        """
        Get the application specific usage and description strings for this module.

        @return tuple with:
        @return - usage string
        @return - description string
        """
        usage = '%prog [options] -- old_pinball new_pinball'
        desc = 'Generate a new recording/pinball by replaying a existing pinball '\
               'with a set of options which describe a new pinball.'

        return usage, desc

    def GetArgs(self, args, parser, options):
        """
        Get arguments passed to the script. The relog script requires two
        command line arguments:

            replay_pinball and log_pinball

        @param args List of arguments from cmd line
        @param parser Command line parser
        @param options Options given on cmd line

        @return tuple with:
        @return - pinball to be replayed
        @return - pinball for relogging
        """

        # Need to get the old and new pinball names.
        #
        if len(args) != 2:
            parser.error('Must give arguments: old_pinball new_pinball')
        replay_pb = args[0]
        log_pb = args[1]

        return replay_pb, log_pb

    def GetPintoolOptions(self, replay_pb, log_pb, options):
        """
        Add the default knobs required for logging and any user defined options.

        @param replay_pb Pinball to replay
        @param log_pb New pinball created by relogging
        @param options Options given on cmd line

        @return string of options
        """

        log_o = ' -log -log:basename ' + log_pb
        log_o += config.drdebug_base_log_options
        log_o += util.AddMt(options, replay_pb)

        # Parameter '--log_options' has some knobs which are always used and
        # might have additional knobs from the user.
        #
        if hasattr(options, 'pintool_options') and options.pintool_options:
            log_o += ' ' + options.pintool_options
        log_o = ' --log_options "%s"' % (log_o)

        return log_o


def main():
    """
   This method allows the script to be run in stand alone mode.

   @return Exit code from running the script
   """

    relog = Relog()
    result = relog.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
