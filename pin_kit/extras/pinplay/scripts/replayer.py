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
# $Id: replayer.py,v 1.67 2015/08/15 20:02:00 tmstall Exp tmstall $

# This is a script to replay one pinplay process
#

import sys
import os
import optparse
import subprocess

# Local modules
#
import msg
import kit
import cmd_options
import config
import util


class Replayer(object):
    """ Replays one set of log files.
        This class is the low level primative which replays one pinball.
    """

    def ParseCommandLine(self):
        """
        Process command line arguments, get Kit, tool options, and their paths.

        @return Tuple containing:
        @return - pin option
        @return - pintool options
        @return - pinball to be replayed
        @return - kit instance
        """

        # import pdb;  pdb.set_trace()
        version = '$Revision: 1.67 $'
        version = version.replace(' ', '')
        ver = version.replace(' $', '')
        us = '%prog [options] pinball_basename \nVersion: ' + ver
        desc = 'Replays one pinball. Use \'--replay_options\' or ' \
               '\'--log_options\' to modify the pintool behavior during replay.'
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
        cmd_options.arch(parser, '')
        cmd_options.config_file(parser)
        cmd_options.cross_os(parser, '')
        cmd_options.debug(parser)
        cmd_options.global_file(parser)
        cmd_options.log_options(parser)
        cmd_options.msgfile_ext(parser)
        cmd_options.no_print_cmd(parser)
        cmd_options.pintool(parser)
        cmd_options.pintool_help(parser)
        cmd_options.pin_options(parser)
        cmd_options.pinplayhome(parser, '')
        cmd_options.playout(parser)
        cmd_options.replay_file(parser)
        cmd_options.replay_options(parser)
        cmd_options.save_global(parser)
        cmd_options.sdehome(parser, '')
        cmd_options.verbose(parser)

        # import pdb;  pdb.set_trace()
        (options, args) = parser.parse_args()

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        if options.verbose:
            msg.PrintMsg('Started replayer.py')
        # Check to make sure the pinball basename has been given as an argument or
        # command line option.
        #
        # import pdb;  pdb.set_trace()
        if options.replay_file == '' and \
           not (hasattr(options, 'pintool_help') and options.pintool_help):
            if len(sys.argv) == 1 or len(args) == 0:
                msg.PrintMsg(
                    "ERROR: Must have a trace basename on the command line.\n"
                    "Usage: %s [options] pinball_basename" % os.path.basename(
                        sys.argv[0]))
                util.CheckResult(-1, options, 'Checking command line options')
            options.replay_file = args[0]

        # Read in an optional configuration files and set global variables.
        #
        config_obj = config.ConfigClass()
        config_obj.GetCfgGlobals(options,
                                 False)  # Don't need to require 4 variables

        # Once the tracing configuration parameters are read, get the kit in
        # case pinplayhome was set on the command line.
        #
        # import pdb;  pdb.set_trace()
        kit_obj = self.GetKit()

        # Translate the 'arch' string given by the user into
        # the internal arch type used by the scripts.
        #
        util.SetArch(options)

        # Now that we know the type of the binary, set the user defined pintool,
        # if one exists.  Need to wait until now to set the tool because the
        # user may only have the tool in the architecture dependent directory
        # for this type of application.  Thus we need the binary type in order
        # to find it.
        #
        # import pdb;  pdb.set_trace()
        kit_obj.binary_type = options.arch

        # If the user specified a pintool, replace the default pintool in the kit with
        # it.
        #
        if hasattr(options, "pintool") and options.pintool:
            kit_obj.SetPinTool(options.pintool, options.replay_file)

        # If user just wants 'pintool_help' go ahead and print it, then exit
        # the script.  Need to do this after we get the kit in order to print
        # the help for the correct kit.  Also needs to be after any user
        # defined pintools have been added to the kit. This ensures the
        # correct pintool help msg will be displayed.
        #
        if hasattr(options, 'pintool_help') and options.pintool_help:
            result = util.PintoolHelpKit(kit_obj, options)

            sys.exit(result)

        pin_options = ''
        pintool_options = ''

        # Check to see if there is a pinball to replay.
        #
        if options.replay_file == "":
            msg.PrintHelpAndExit('Replay file not specified!')

        platform = util.Platform()
        if platform == config.LINUX:
            pin_options = ' ' + kit_obj.prefix + ' -xyzzy '

            # If using NOT using Linux tools to work with whole program pinballs generated on Windows,
            # then need a set of  knobs for the pin binary itself.
            #
            if not options.cross_os:
                pin_options += kit_obj.prefix + ' -reserve_memory '
                pin_options += kit_obj.prefix + ' ' + options.replay_file + '.address '

        pintool_options += ' -replay:basename ' + options.replay_file
        if options.playout or '-replay:playout 1' in options.replay_options:
            # If present, need to remove the knob '-replay:playout 1' from
            # options.replay_options because it can only be given once on the
            # command line.
            #
            pintool_options += ' -replay:playout 1 '
            options.replay_options = options.replay_options.replace(
                '-replay:playout 1', '')
        else:
            pintool_options += ' -replay:playout 0 '

        # If running Windows WP pinballs on Linux, then need this knob for the replayer pintool.
        #
        if options.cross_os:
            pintool_options += ' -replay:addr_trans'

        # Add knobs for Pin and replay/logging user gave on the command line.
        #
        pin_options += ' ' + options.pin_options
        pintool_options += ' ' + options.replay_options + ' ' + options.log_options

        # If user has log options, then may need to at multi-thread knob.
        #
        if options.log_options:
            pintool_options += util.AddMt(options, options.replay_file)

        return pin_options, pintool_options, options.replay_file, kit_obj, options

    def GetKit(self):
        """
        Get the PinPlay kit.

        @return PinPlay kit
        """

        return kit.Kit()

    def Run(self):
        """
        Get all the user options and run the replayer.

        @return Exit code from the replayer pintool
        """

        # import pdb;  pdb.set_trace()
        parsed_tool_opts, parsed_pintool_opts, basename, kit_obj, options = self.ParseCommandLine()

        # Print out the version number
        #
        if config.debug:
            print os.path.basename(sys.argv[0]) + " $Revision: 1.67 $"

        # Get path to the kit tool and the appropriate pintool name
        #
        cmd = os.path.join(kit_obj.path, kit_obj.pin)

        # Print out all the parsed options
        #
        if config.debug:
            print "parsed_tool_opts:    ", parsed_tool_opts
            print "parsed_pintool_opts: ", parsed_pintool_opts
            print "basename:            ", basename
            print

        # Kit tool options
        #
        pin_options = ''
        pin_options += parsed_tool_opts

        # Pintool options, including the base logging options for all runs.
        # Also add any default knobs from the kit, if they exist.
        #
        pintool_options = ''
        pintool_options = ' -replay -xyzzy '
        pintool_options += parsed_pintool_opts
        pintool_options += util.GetMsgFileOption(basename)
        # import pdb;  pdb.set_trace()
        if kit_obj.default_knobs:
            pintool_options += ' ' + kit_obj.default_knobs

        # If generating instruction mix (indicated by the knobs -mix/-omix),
        # then can not use a pintool (a restriction imposed by the SDE driver).
        # Otherwise, get the appropriate pintool.
        #
        # import pdb;  pdb.set_trace()
        cmd = cmd + pin_options
        if '-mix' not in parsed_pintool_opts and '-omix' not in parsed_pintool_opts:
            cmd += kit_obj.GetPinToolKnob(basename)
        cmd += pintool_options

        # Add nullapp
        #
        cmd = cmd + ' -- ' + kit_obj.GetNullapp(basename)

        # Remove any double quotes (") which still exist in the cmd.
        #
        cmd = cmd.replace('"', '')

        # Finally execute the command line and gather stdin and stdout
        # Exit with the return code from executing the logger.
        #
        # import pdb;  pdb.set_trace()
        result = 0

        # Print out command line used for pin and pintool
        #
        # import pdb;  pdb.set_trace()
        if not (hasattr(options, 'no_print_cmd') and
                options.no_print_cmd) or options.verbose:
            if options.verbose:
                msg.PrintMsgNoCR('Starting job: %s\n' % cmd)
            else:
                msg.PrintMsg('\n%s' % cmd)

        if not config.debug:
            p = subprocess.Popen(cmd, shell=True)
            if options.verbose:
                msg.PrintMsg('Job PID: %d' % p.pid)
            (stdout, stderr) = p.communicate()
            result = p.returncode
            if options.verbose:
                msg.PrintMsg('Job finished, PID: %d' % p.pid)

        return result


def main():
    """
    Process command line arguments and run the replayer

    @return Exit code from running the script
    """

    replayer = Replayer()
    result = replayer.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
