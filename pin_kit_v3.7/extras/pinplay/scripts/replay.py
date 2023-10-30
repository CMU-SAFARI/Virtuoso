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
# This script must be invoked with an explict path name in order to wor
# correctly.  It will NOT work if the directory containing the script
# is found via the environment variable PATH.
#
#
# $Id: replay.py,v 1.15 2015/12/08 18:01:25 tmstall Exp tmstall $

import sys
import os
import optparse
import time

# Local modules
#
import cmd_options
import config
import drd_util
import msg
import util


class Replay(object):
    """
    Replay DrDebug log files.
    """

    def GetHelpParams(self):
        """
        Get the application specific usage and description strings for this module.

        @return tuple with:
        @return - usage string
        @return - description string
        """
        usage = '%prog [options] -- pinball'
        desc = 'Replay a recording (pinball).'

        return usage, desc

    def GetArgs(self, args, parser, options):
        """
        Get arguments passed to the script.  Method must returns two values.
        However, in this module, only the first return value is used.  Thus the
        2nd return value is 'None'.

        Need two values because the relog class method requires two return values.

        @param args List of arguments from cmd line
        @param parser Command line parser
        @param options Options given on cmd line

        @return tuple with:
        @return - pinball to be replayed
        @return - pinball for relogging (None in this case)
        """

        # Need to get the old and new pinball names.
        #
        if len(args) != 1:
            parser.error(
                'Must give arguments using this format: -- pinball_name')
        replay_pb = args[0]
        log_pb = None

        return replay_pb, log_pb

    def AddAdditionalOptions(self, parser):
        """
        There are no additional options for non-GDB scripts.  This is only a stub.

        @return No return value
        """

        return

    def ParseCommandLine(self):
        """
        Parse command line arguments.

        @return tuple with:
        @return - options given on cmd line
        @return - parser for command line
        """

        version = '$Revision: 1.15 $'
        version = version.replace(' ', '')
        ver = version.replace(' $', '')
        us, desc = self.GetHelpParams()

        parser = optparse.OptionParser(
            usage=us,
            description=desc,
            version=ver,
            formatter=cmd_options.BlankLinesIndentedHelpFormatter())

        # Define the command line options which control the behavior of the
        # script.  Some of these methods take a 2nd argument which is the empty
        # string ''.   If the script uses option groups, then this parameter is
        # the group.  However, this script does not use option groups, so the
        # argument is empty.
        #
        drd_util.debug(parser)
        cmd_options.arch(parser, '')
        cmd_options.cross_os(parser, '')
        self.AddAdditionalOptions(parser)  # Add script dependent options
        cmd_options.pin_options(parser)
        cmd_options.pintool_help(parser)
        cmd_options.pintool_options(parser)
        cmd_options.pintool(parser)
        cmd_options.verbose(parser)

        # import pdb;  pdb.set_trace()
        (options, args) = parser.parse_args()

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        # Read in configuration files and set global variables.
        # No requirment to read in a config file, but it's OK
        # to give one.
        #
        config_obj = config.ConfigClass()
        config_obj.GetCfgGlobals(options,
                                 False)  # No, don't need required parameters

        # If user just wants 'pintool_help', don't look for command line
        # but instead return from this function.
        #
        if not (hasattr(options, 'pintool_help') and options.pintool_help):

            # Get the binary and arguments to run in 'options.command'
            # and the old.  May also get new pinball name if necessary.
            #
            replay_pb, log_pb = self.GetArgs(args, parser, options)

            return options, replay_pb, log_pb
        else:
            return options, None, None

    def AddPinKnobs(self, options):
        """
        No pin knobs to add.

        @return empty string
        """

        return ''

    def Initialize(self, options):
        """
        Do nothing.

        @return no return
        """

    def GetPintoolOptions(self, replay_pb, log_pb, options):
        """
        Add the default knobs required for replaying and any user defined options.

        @param replay_pb Not used
        @param log_pb Not used
        @param options Options given on cmd line

        @return string of options
        """

        replay_o = ''
        if hasattr(options, 'pintool_options') and options.pintool_options:
            replay_o = ' --replay_options "%s"' % (options.pintool_options)

        return replay_o

    def RunScript(self, cmd, options):
        """
        Run the script in the foreground.

        @param cmd Command line to run lower level script
        @param options Options given on cmd line

        @return error code from running script
        """

        return drd_util.RunScriptFore(cmd, options)

    def Finalize(self, kit_script_path, options):
        """
        Run code required to complete the 'base' scripts (no GDB).

        @param kit_script_path Explicit path to location in kit where scripts are located
        @param options Options given on cmd line

        @return error code
        """

        return drd_util.FinalizeNoGDB(kit_script_path, options)

    def Run(self):
        """
        Get all the user options and relog the pinball.

        @return exit code from this script
        """

        options, replay_pb, log_pb = self.ParseCommandLine()

        # Add the directory where the script is executed to PATH
        #
        util.AddScriptPath()

        # Get any Pin knobs (not pintool) specific to this module.
        #
        pin_knobs = self.AddPinKnobs(options)

        # Get the script and options require to run the low levels script for
        # replaying a pinball.  NOTE: return value 'kit_script_path' is not used.
        #
        cmd, kit_script_path = drd_util.DrDebugScriptCmd('replayer.py',
                                                         pin_knobs, options)

        # If use just wants 'pintool_help', then call the appropriate
        # low level script with this option.  Then exit this script.
        #
        if hasattr(options, 'pintool_help') and options.pintool_help:
            result = drd_util.PintoolHelpCmd(cmd, options)

            return result

        # Module specific setup
        #
        self.Initialize(options)

        # Add any pintool options given on the command line.
        #
        cmd += self.GetPintoolOptions(replay_pb, log_pb, options)

        # Don't print the Pin/Pintool command
        #
        cmd += ' --no_print_cmd'

        # We want the application output when we replay it.
        #
        cmd += ' --playout'

        # Add optional arguments the user may have passed on the command line
        #
        if hasattr(options, 'arch') and options.arch:
            cmd += ' --arch ' + options.arch
        if hasattr(options, 'cross_os') and options.cross_os:
            cmd += ' --cross_os '
        if hasattr(options, 'pintool') and options.pintool:
            cmd += ' --pintool ' + options.pintool
        if hasattr(options, 'verbose') and options.verbose:
            cmd += ' --verbose '

        # Add the configuration files if needed
        #
        cmd += util.AddCfgFile(options)

        # Add the name of the old pinball to replay
        #
        cmd += ' %s' % (replay_pb)

        # Print the script command line
        #
        if hasattr(options, 'verbose') and options.verbose or \
           hasattr(options, 'debug') and options.debug:
            string = '\n' + cmd
            msg.PrintMsg(string)

        # Run low level script
        #
        result = self.RunScript(cmd, options)

        # Final phase of the script is module specific.
        #
        result = self.Finalize(kit_script_path, options)

        return result


def main():
    """
       This method allows the script to be run in stand alone mode.

       @return exit code from running this script
       """

    replay = Replay()
    result = replay.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
