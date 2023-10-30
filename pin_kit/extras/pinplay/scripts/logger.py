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
# $Id: logger.py,v 1.84 2015/08/15 20:02:00 tmstall Exp tmstall $

import sys
import os
import random
import subprocess
import optparse

# Local modules
#
import cmd_options
import config
import kit
import msg
import util


class Logger(object):
    """
    Generate log files for an application

    This class is the mid level primitive which runs an application with the
    logger to generate pinballs (log files) for the PinPlay/SDE apps.  It works
    with these classes of applications:

        single-threaded
        multi-threaded
        MPI single-threaded
        MPI multi-threaded
        multi-process single-threaded (which use fork/exec to create new proc)
        multi-process multi-threaded (which use fork/exec to create new proc)

    Calls the lowest level script log.py to actually run pin/logger.
    """

    # Class attributes
    #
    log_cmd = 'log.py'
    gv = config.GlobalVar()

    def AddOptionIfNotPresent(self, options, opt, arg):
        """
        This function adds an option to the tool if the option was not already
        passed in the --log_options switch

        @param options Options given on cmd line
        @param opt     Option to add
        @param arg     Argument for option

        @return String containing option and argument
        """

        # import pdb;  pdb.set_trace()
        raw_options = options.log_options

        # option was not passed in the command line
        #
        if raw_options.find(opt) == -1:
            return ' ' + opt + ' ' + arg + ' '
        return ''

    def ParseCommandLine(self):
        """
        Parse command line arguments and returns pin and tools options and app command line.

        @return List containing: options
        """

        # command line options for the driver
        #
        version = '$Revision: 1.84 $'
        version = version.replace('$Revision: ', '')
        ver = version.replace(' $', '')
        us = '%prog [options] --mode MODE --log_file FILE binary args'
        desc = 'Runs binary with the logger Pintool and generates whole program '\
               'pinballs. Required arguments include:\n'\
               '   1) Binary to run and it\'s arguments.\n'\
               '   2) Name for the log file (pinball) using \'--log_file FILE\'\n'\
               '         Name can include a dir as well as a file.\n' \
               '   3) Mode of the binary using \'--mode MODE\'.'

        util.CheckNonPrintChar(sys.argv)
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

        cmd_options.compressed(parser, '')
        cmd_options.config_file(parser)
        cmd_options.debug(parser)
        cmd_options.global_file(parser)
        cmd_options.list(parser, '')
        cmd_options.log_file(parser)
        cmd_options.log_options(parser)
        cmd_options.mode(parser, '')
        cmd_options.mpi_options(parser, '')
        cmd_options.msgfile_ext(parser)
        cmd_options.no_log(parser)
        cmd_options.num_proc(parser, '')
        cmd_options.pid(parser)
        cmd_options.pinplayhome(parser, '')
        cmd_options.pintool(parser)
        cmd_options.pin_options(parser)
        cmd_options.save_global(parser)
        cmd_options.sdehome(parser, '')
        cmd_options.verbose(parser)

        # import pdb;  pdb.set_trace()
        (options, args) = parser.parse_args()

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        # Read in configuration files and set global variables.
        # No need to read in a config file.
        #
        config_obj = config.ConfigClass()
        config_obj.GetCfgGlobals(options,
                                 False)  # No, don't need required parameters

        # Make sure user gave an application mode
        #
        if options.mode:
            options.mode = util.ParseMode(options.mode)
        else:
            parser.error(
                "Application mode was not given.\n"
                "Need to use option --mode MODE. Choose MODE from: 'st', 'mt', 'mpi', 'mpi_mt', 'mp', 'mp_mt',")

        # Log file name must be given.
        #
        # import pdb;  pdb.set_trace()
        if options.log_file == '':
            parser.error('Log file basename was not given.\n' \
                'Must give basename with option: --log_file FILE')

        # Get the application command line
        #
        # import pdb;  pdb.set_trace()
        cmd_line = " ".join(args)
        if not options.pid:
            if cmd_line:
                setattr(options, 'command', cmd_line)
            else:
                parser.error('no program command line specified.\n'
                             'Need to add binary and it\'s arguments.')

        return options

    def GetKit(self, parser):
        """
        Get the PinPlay kit.

        @param parser Command line parser

        @return PinPlay kit
        """

        return kit.Kit()

    def Run(self):
        """
        Get all the user options and run the logger.

        @return Exit code from the logger pintool
        """

        # import pdb;  pdb.set_trace()
        options = self.ParseCommandLine()

        # Script which will actually do the logging
        #
        cmd = self.log_cmd

        # If the user has given these options, add them
        #
        if hasattr(options, 'compressed') and options.compressed:
            cmd += ' --compressed=%s ' % options.compressed
        if hasattr(options, 'log_file') and options.log_file:
            cmd += ' --log_file ' + options.log_file
        log_opts = ' --log_options "-log:syminfo -log:pid '
        if hasattr(options, 'log_options') and options.log_options:
            log_opts += '%s" ' % options.log_options
        else:
            log_opts += '"'
        cmd += log_opts
        if hasattr(options, 'no_log') and options.no_log:
            cmd += ' --no_log '
        if hasattr(options, 'pid') and options.pid:
            cmd += ' --pid ' + str(options.pid)

        cmd += util.AddGlobalFile(self.gv.DumpGlobalVars(), options)
        cmd += util.AddCfgFile(options)

        # Finally add program and arguments 
        #
        # cmd += ' ' + options.command

        # If the command is not already in double quotes, then quote it now.
        # This takes care of cases where the command may redirect input or output
        # or the command has options which contain the char '-'.
        #
        # Assume if there is one double quote, then a 2nd double quote should also
        # already be in the command.
        #
        if not (hasattr(options, 'pid') and options.pid):
            if options.command.find('"') == -1:
                cmd += ' -- "' + options.command + '" '
            else:
                cmd += ' -- ' + options.command

        # Print out command line used for pin and pintool
        #
        string = '\n' + cmd
        msg.PrintMsg(string)

        # Finally execute the command line and gather stdin and stdout.
        # Exit with the return code from executing the logger.
        #
        result = 0
        # import pdb;  pdb.set_trace()
        if not config.debug and not options.list:
            platform = util.Platform()
            if platform != config.WIN_NATIVE:
                cmd = 'time ' + cmd
            p = subprocess.Popen(cmd, shell=True)
            p.communicate()
            result = p.returncode

        return result


def main():
    """
       This method allows the script to be run in stand alone mode.

       @return Exit code from running the script
       """

    logger = Logger()
    result = logger.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
