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
# $Id: record.py,v 1.29 2015/12/08 18:01:25 tmstall Exp tmstall $

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
import util


class Record(object):
    """
    Generate log files for DrDebug.
    """

    def GetHelpParams(self):
        """
        Get the application specific usage and description strings for this module.

        @return tuple with:
        @return - usage string
        @return - description string
        """

        usage = '%prog [options] -- binary args\n              or\n'\
             '       %prog [options] --pid PID'
        desc = 'Create a recording (pinball).  There are two modes:\n'\
                '  1) Give command line of a binary to record\n'\
                '  2) Give the PID of a running process'

        return usage, desc

    def GetArgs(self, args, parser, options):
        """
        Get binary and arguments passed to the script.

        Must include either binary/options or '--pid', but not both.  The
        command line is added to the 'options'.

        @param args List of arguments from cmd line
        @param parser Command line parser
        @param options Options given on cmd line

        @return no return value
        """

        num_args = len(args)
        if (hasattr(options, 'pid') and options.pid):
            # If user gave --pid, then no command line allowed.
            #
            if num_args > 0:
                parser.error('No binary or arguments allowed with \'--pid\'')
        elif num_args == 0:
            # Else must have given a command line.
            #
            parser.error('Must either: include binary and arguments after the \n'\
                         'string \'--\' or use option \'--pid PID\'.')
        cmd_line = " ".join(args)
        setattr(options, 'command', cmd_line)

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

        version = '$Revision: 1.29 $'
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
        cmd_options.config_file(parser)
        self.AddAdditionalOptions(parser)  # Add script dependent options
        cmd_options.log_file(parser)
        cmd_options.mp_type(parser, '')
        cmd_options.pid(parser)
        cmd_options.pin_options(parser)
        cmd_options.pintool_help(parser)
        cmd_options.pintool_options(parser)
        cmd_options.pintool(parser)
        cmd_options.single_thread(parser)
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

        if not options.single_thread:
            if options.mp_type == 'mpi':
                opt_mode = config.MPI_MT_MODE
            elif options.mp_type == 'mp':
                opt_mode = config.MP_MT_MODE
            else:
                opt_mode = config.MT_MODE
        else:
            if options.mp_type == 'mpi':
                opt_mode = config.MPI_MODE
            elif options.mp_type == 'mp':
                opt_mode = config.MP_MODE
            else:
                opt_mode = config.ST_MODE
        setattr(options, 'mode', opt_mode)

        # If user just wants 'pintool_help', don't look for command line
        # but instead return.  Otherwise get binary and arguments.
        #
        if not (hasattr(options, 'pintool_help') and options.pintool_help):
            # Get the binary and arguments to run in 'options.command'
            #
            self.GetArgs(args, parser, options)

        return options, parser

    def GetDrdLogOpt(self):
        """
        Get the logging options required for DrDebug.

        @return string of knobs
        """

        return config.drdebug_base_log_options

    def AddPinKnobs(self, options):
        """
        No knobs to add.  However must return a string so just return the
        empty string.

        @return empty string
        """

        return ''

    def Initialize(self, options):
        """
        Nothing to initialize, just return

        @return no return value
        """

        return

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
        Complete the remainder of the actions of the script.

        @param kit_script_path Explicit path to location in kit where scripts are located
        @param options Options given on cmd line

        @return error code
        """

        return drd_util.FinalizeNoGDB(kit_script_path, options)

    def Run(self):
        """
        Get the user options, format command line for the lower level script and
        run it.

        @return exit code from lower level script
        """

        options, parser = self.ParseCommandLine()

        # Add the directory where this script is located to PATH so other
        # Python scripts can be run from this kit.
        #
        util.AddScriptPath()

        # Get any Pin knobs (not pintool) specific to this module.
        #
        pin_knobs = self.AddPinKnobs(options)

        # Get the script and options require to run the low levels script for
        # logging.
        #
        # import pdb;  pdb.set_trace()
        cmd, kit_script_path = drd_util.DrDebugScriptCmd('log.py', pin_knobs,
                                                         options)

        # If use just wants 'pintool_help', then call the appropriate low level
        # script with this option.  Then exit this script.  This needs to be
        # after the call to drd_util.DrDebugScriptCmd() in order to run the
        # script for the correct kit.
        #
        if hasattr(options, 'pintool_help') and options.pintool_help:
            result = drd_util.PintoolHelpCmd(cmd, options)

            return result

        # Module specific setup
        #
        self.Initialize(options)

        # Always use these options to the script
        #
        cmd += ' --no_print_cmd --mode ' + options.mode

        # Option '--log_options' needs some knobs which are always used
        # and also might have additional knobs from the user.
        #
        log_o = self.GetDrdLogOpt()
        if hasattr(options, 'pintool_options') and options.pintool_options:
            log_o += ' ' + options.pintool_options
        if hasattr(options, 'mode') and options.mode:
            # Need to add PID to pinball name for multi-process log files.
            #
            if options.mode == config.MPI_MODE or \
               options.mode == config.MPI_MT_MODE or \
               options.mode == config.MP_MODE or \
               options.mode == config.MP_MT_MODE:
                log_o += ' -log:pid'
        cmd += ' --log_options "%s"' % (log_o)

        # Add optional arguments the user may have passed on the command line
        #
        if hasattr(options, 'arch') and options.arch:
            cmd += ' --arch ' + options.arch
        if hasattr(options, 'log_file') and options.log_file:
            cmd += ' --log_file ' + options.log_file
        else:
            cmd += ' --log_file pinball/log'  # Default name
        if hasattr(options, 'pid') and options.pid:
            cmd += ' --pid ' + str(options.pid)
        if hasattr(options, 'verbose') and options.verbose:
            cmd += ' --verbose '

        # Add the configuration files, if needed
        #
        cmd += util.AddCfgFile(options)

        # Add the binary to the command line.
        #
        # If the command is not already in double quotes, then quote it now.
        # This takes care of cases where the command may redirect input or output
        # or the command has options which contain the char '-'.
        #
        # Assume if there is one double quote, then a 2nd double quote should
        # also already be in the command.
        #
        if not (hasattr(options, 'pid') and options.pid):
            if options.command.find('"') == -1:
                cmd += ' "' + options.command + '" '
            else:
                cmd += ' ' + options.command

        # Print the script command line
        #
        if hasattr(options, 'verbose') and options.verbose or \
           hasattr(options, 'debug') and options.debug:
            string = '\n' + cmd
            msg.PrintMsg(string)

        # Run low level script.  Exit with result from
        # running the script.
        #
        result = self.RunScript(cmd, options)

        # Final phase of the script is module specific.
        #
        self.Finalize(kit_script_path, options)

        return result


def main():
    """
    This method allows the script to be run in stand alone mode.

    @return Exit code from running the script
    """

    record = Record()
    result = record.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
