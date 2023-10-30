#!/usr/bin/env python
#
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
# $Id: sim_run_dir.py,v 1.13 2015/05/19 19:48:55 tmstall Exp tmstall $

import sys
import os

# Local modules
#
import cmd_options
import config
import msg
import replay_dir
import sim_kit
import sim_run
import util


class SimRunMulti(replay_dir.ReplayMulti):
    """
    Run the simulator on multiple LIT files.

    This class is a wrapper which processes either one LIT file or the
    set of LIT files in a directory
    """

    # Script to replay one LIT file.
    #
    replayer_cmd = 'sim_run.py'

    def GetKit(self):
        """ Get the SDE kit. """

        return sim_kit.SimKit()

    def AddAdditionalOptions(self, parser):
        """Add any additional options besides the default for all *_dir.py scripts."""

        cmd_options.processor(parser, '')
        cmd_options.simhome(parser, '')
        cmd_options.sim_options(parser, '')
        cmd_options.verify(parser, '')

    def Replay(self, param, dirname, filename):
        """
        Run the Simulator on a single LIT file given the command line options and the name of
        the file to run. It formats the appropriate command line options,
        saves global variables in a pickle file & calls the Sim run script.
        """

        # import pdb ; pdb.set_trace()
        if param.has_key('options'):
            options = param['options']
        else:
            msg.PrintAndExit(
                'method replay_dir.Replay() failed to get param \'options\'')
        basename_file = os.path.join(dirname, filename)
        if config.verbose:
            msg.PrintMsg("-> Replaying pinball \"" + basename + "\"")

        cmd = self.replayer_cmd + ' ' + basename_file

        # Check to see if need to add options passed to the Sim run script.
        # These are NOT parameters, so they don't get passed in the global
        # variables.
        #
        if options.sim_options:
            cmd += ' --sim_options ' + options.sim_options
        if options.verify:
            cmd += ' --verify'

        # Add the configuration file, if one exists and print the cmd if just
        # debugging.
        #
        # import pdb ; pdb.set_trace()
        cmd += util.AddCfgFile(options)
        if options.debug:
            msg.PrintMsg(cmd)

        result = 0
        if not options.debug:

            # Dump the global data to a unique file name.  Need to add the
            # option --global_file with this unique file name to options when
            # calling a script.
            #
            gv = config.GlobalVar()
            cmd += util.AddGlobalFile(gv.DumpGlobalVars(), options)

            # import pdb ; pdb.set_trace()
            if not options.list:
                result = util.RunCmd(cmd, options, filename,
                                     concurrent=True)  # Run concurrent jobs here
            else:
                msg.PrintMsg(cmd)

        return result


def main():
    """ Process command line arguments and run the replayer """

    replay = SimRunMulti()
    result = replay.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
