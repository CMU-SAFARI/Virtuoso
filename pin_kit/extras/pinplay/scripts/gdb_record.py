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
# $Id: gdb_record.py,v 1.8 2015/12/08 18:01:25 tmstall Exp tmstall $

import sys
import os
import subprocess
import optparse
import re
import time

# Local modules
#
import cmd_options
import config
import drd_util
import msg
import record
import util


class GdbRecord(record.Record):
    """
    Generate log files for DrDebug inside GDB.
    """

    def GetHelpParams(self):
        """
        Get the application specific usage and description strings for this module.

        @return tuple with:
        @return - usage string
        @return - description string
        """

        usage = '%prog [options] -- binary args\n              or\n'\
             '       %prog [options] --pid PID -- binary # no args'
        desc = 'Create a recording (pinball).  There are two modes:\n'\
                '  1) Give command line (including binary & arguments) to record\n'\
                '  2) Give the PID of a running process and the binary of the process\n'\
                '     (no arguments to binary)'

        return usage, desc

    def GetArgs(self, args, parser, options):
        """
        Get binary and arguments passed to the script.   If using option
        '--pid' then can only have the binary.  Otherwise can give command line
        with binary/options.

        NOTE: Side effect adds command line to 'options'.

        @param args List of arguments from cmd line
        @param parser Command line parser
        @param options Options given on cmd line

        @return no return value
        """

        num_args = len(args)
        if (hasattr(options, 'pid') and options.pid):
            # If user gave --pid, then command line must be just the binary.
            #
            if num_args > 1:
                parser.error('Only binary, with no arguments, allowed on command\n'\
                             'line with \'--pid\'.')
            elif num_args == 0:
                parser.error('Must give a binary with \'--pid PID\'.')
        elif num_args == 0:
            # User has not given a command line.
            #
            parser.error('Must either: Include binary and arguments after\n'\
                         'string \'--\' or use option \'--pid PID\', the string \'--\' and binary.')

        # Check to make sure the binary exists and is executable.
        #
        cmd = args[0]
        if not util.Which(cmd):
            parser.error(
                'Binary \'%s\' either not found, or not executable' % cmd)

        # Add the command line to the options
        #
        cmd_line = " ".join(args)
        setattr(options, 'command', cmd_line)

    def AddAdditionalOptions(self, parser):
        """
        Add additional default options which GDB version of scripts need.

        @param parser Optparse object

        @return No return value
        """

        drd_util.AddAdditionalOptions(parser)

    def GetDrdLogOpt(self):
        """
        Get the GDB specific DrDebug logging options.

        When using GDB, there are additional knobs required in addition to the
        basic logging knobs.

        @return string with knobs for logging
        """

        return drd_util.GdbBaseLogOpt()

    def AddPinKnobs(self, options):
        """
        Add pin knobs (not pintool knobs) needed when running with GDB.

        @return string of knobs
        """

        return drd_util.AddPinKnobsGDB(options)

    def Initialize(self, options):
        """
        Initialize GDB and do some basic checks to see how well this version of
        GDB will work with the script.  Don't need to check the GDB version for
        recording, only replay.
        """

        drd_util.GdbInitialize(options, check_version=False)

    def RunScript(self, cmd, options):
        """
        When running with GDB, run the script in the background, not the foreground

        @param cmd Command line to run lower level script
        @param options Options given on cmd line

        @return error code from running script
        """

        return drd_util.RunScriptBack(cmd, options)

    def Finalize(self, kit_script_path, options):
        """
        Run code required to complete the GDB scripts.

        @param kit_script_path Explicit path to location in kit where scripts are located
        @param options Options given on cmd line

        @return Exit code from running GDB on the application
        """

        return drd_util.FinalizeGDB(kit_script_path, options)


def main():
    """
    This method allows the script to be run in stand alone mode.

    @return Exit code
    """

    gdb_record = GdbRecord()
    result = gdb_record.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
