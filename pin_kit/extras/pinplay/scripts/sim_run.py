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
# $Id: sim_run.py,v 1.16 2015/08/15 20:02:00 tmstall Exp tmstall $

# This is a script to run a simulator on one trace.
#

import sys
import os
import optparse
import subprocess
import glob

# Local modules
#
import cmd_options
import config
import msg
import sim_kit
import util


class SimRun(object):
    """
    This class is the low level primative which runs a simulator on one set of trace files.
    """

    kit = None
    Config = config.ConfigClass()

    def AddAdditionalCmdOptions(self, parser):
        """There are no additional options for the base clase simulator.  This is only a stub."""

        return

    def ParseCommandLine(self):
        """
        Process command line arguments. Generate pin and tool options, and their paths.
        """

        # import pdb;  pdb.set_trace()
        version = '$Revision: 1.16 $'
        version = version.replace(' ', '')
        ver = version.replace(' $', '')
        us = '%prog [options] trace_basename \nVersion: ' + ver
        desc = 'Run a simulator on a trace.'
        util.CheckNonPrintChar(sys.argv)
        parser = optparse.OptionParser(usage=us, version=ver, description=desc)

        # Define the command line options which control the behavior of the
        # script.
        #
        # Some of these methods take a 2nd argument which is the empty string
        # ''.  If this script used option groups, then the 2nd parameter would
        # be the group.  However, this script does not use option groups, so
        # the argument is empty.
        #
        cmd_options.config_file(parser)
        cmd_options.debug(parser)
        cmd_options.global_file(parser)
        cmd_options.list(parser, '')
        cmd_options.processor(parser, '')
        cmd_options.save_global(parser)
        cmd_options.simhome(parser, '')
        cmd_options.sim_options(parser, '')
        cmd_options.trace_basename(parser)
        cmd_options.verbose(parser)
        cmd_options.verify(parser, '')

        self.AddAdditionalCmdOptions(parser)

        # import pdb;  pdb.set_trace()
        (options, args) = parser.parse_args()

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        # Check to make sure the trace basename has been given as an argument.
        #
        # import pdb;  pdb.set_trace()
        if len(sys.argv) == 1 or len(args) == 0:
            msg.PrintMsg(
                "ERROR: Must have a trace basename on the command line.\n"
                "Usage: %s [options] trace_basename" % os.path.basename(
                    sys.argv[0]))
            util.CheckResult(-1, options, 'Checking command line options')
        options.trace_basename = args[0]

        # Read in optional configuration files and set global variables.
        #
        config_obj = config.ConfigClass()
        config_obj.GetCfgGlobals(options,
                                 False)  # Don't need all 4 required parameters

        # Just need this parameter.
        #
        self.Config.CheckRequiredParameter('processor')

        # Initialize the kit after reading in parameters, in case the user
        # specified anything which is used to intialize the kit.
        #
        self.kit = self.GetKit()

        return options

    def GetKit(self):
        """ Get the simulator kit. """

        return sim_kit.SimKit()

    def AddUniqueOptions(self, options):
        """There are no additional options for the base class simulator.  This is only a stub."""

        return ''

    def Run(self):
        """ Get all the user options and run the simulator."""

        # import pdb;  pdb.set_trace()
        options = self.ParseCommandLine()

        # Print out the version number
        #
        if config.debug:
            print os.path.basename(sys.argv[0]) + " $Revision: 1.16 $"

        # Print out all the parsed options
        #
        if config.debug:
            msg.PrintMsg('Trace basename: ' + options.trace_basename)
            msg.PrintMsg('Options:        ' + options.sim_options)

        # Get the number of instructions in the trace.  Set 'add_tid' to
        # True to include TID in the file name for pinballs with a TID.
        #
        # import pdb;  pdb.set_trace()
        field = util.FindResultString(options.trace_basename, 'inscount:', \
            add_tid=True)
        icount = field[0]
        if icount:
            icount = int(icount)
        else:
            icount = 0

        # Explicit path to the binary.
        #
        # import pdb;  pdb.set_trace()
        cmd = os.path.join(self.kit.sim_path, self.kit.binary)

        # Add some options which are not parameters (and thus not in the global
        # data file) to the command line.
        #
        # import pdb;  pdb.set_trace()
        if options.sim_options:
            cmd += ' ' + options.sim_options
        elif self.kit.default_knobs:
            cmd += ' ' + self.kit.default_knobs
        cmd += ' -cfg ' + self.kit.GetSimCfgFile(options)
        cmd += ' -max_litcount ' + str(icount)

        # Add any unique options required by the simulator.
        #
        cmd += self.AddUniqueOptions(options)

        # Add the name of the trace to simulate.
        #
        cmd += ' ' + options.trace_basename

        # Print out command line before removing any double quotes.
        #
        msg.PrintMsg(cmd)

        # Remove any double quotes (") which still exist in the cmd.
        #
        cmd = cmd.replace('"', '')

        if options.verify:
            # If not just listing cmd, print msg letting the user know we are
            # verifying a trace.
            #
            if not options.list:
                msg.PrintMsgPlus('Verifying trace: ' + options.trace_basename)

        # Setup to write stdout/stderr to a log file.
        #
        sim_out_filename = options.trace_basename + config.sim_out_ext
        sim_out_file = open(sim_out_filename, 'w')
        sout = sim_out_file
        serr = sim_out_file

        # Execute the command line and wait for it to execute.  Check for
        # errors if validating a trace.
        #
        # import pdb;  pdb.set_trace()
        result = 0
        if not options.list and not config.debug:

            p = subprocess.Popen(cmd, stdout=sout, stderr=serr, \
                shell=True)
            p.communicate()
            result = p.returncode

            if options.verify:
                # Look for strings in the output file indicating there was an
                # error.
                #
                # import pdb;  pdb.set_trace()
                sim_out_file = open(sim_out_filename, 'r')
                all_lines = sim_out_file.readlines()
                sim_out_file.close()
                error = False
                for line in all_lines:
                    if 'fault' in line or \
                       'Unknown error after executing' in line or \
                       'aborted by microcode after executing' in line or \
                       'knob library:' in line or \
                       'Can\'t open config file' in line:
                        error = True
                        break
                if error:
                    # If got an error, then print a msg and the output.
                    #
                    msg.PrintMsg('ERROR: Verify failed for trace')
                    msg.PrintMsg('   %s\n' % options.trace_basename)
                    for line in all_lines:
                        msg.PrintMsgNoCR(line)
                else:
                    # Else, just print a msg stating it's OK.
                    #
                    msg.PrintMsg('\n%s: OK' % options.trace_basename)

                if not options.verbose:
                    # Just print a few select lines of the output.
                    #
                    for line in all_lines:
                        if 'halted after executing' in line or \
                           '  T0: inst' in line:
                            msg.PrintMsgNoCR(line)
                else:
                    # If verbose, print everything.
                    #
                    for line in all_lines:
                        msg.PrintMsgNoCR(line)

        # Exit with the return code from executing the simulator.
        #
        return result


def main():
    """ Process command line arguments and run the simulator """

    run = SimRun()
    result = run.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
