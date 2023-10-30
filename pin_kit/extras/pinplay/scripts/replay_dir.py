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
# $Id: replay_dir.py,v 1.78 2015/08/15 20:02:00 tmstall Exp tmstall $

import sys
import os
import optparse

# Local modules
#
import cmd_options
import config
import kit
import msg
import util


class ReplayMulti(object):
    """
    Replay multiple pinballs.

    This class is a wrapper which replays multiple pinballs.

    NOTE: To add a new simulator, code in this module needs to be modified.
    Look in the method Replay() for the string: config.sim_kit_type
    """

    # Python script to replay one pinball.
    #
    replayer_cmd = 'replayer.py'

    # Objects needed in this class.
    #
    kit_obj = None

    def GetKit(self):
        """Get the PinPlay kit.

        @return Kit
        """

        return kit.Kit()

    def AddAdditionalOptions(self, parser):
        """
        Add any additional options besides the default for all scripts.

        @param parser Command line parser

        @return No return value
        """

        cmd_options.bb_add_filename(parser)
        cmd_options.log_options(parser)
        cmd_options.pin_options(parser)
        cmd_options.pinplayhome(parser, '')
        cmd_options.playout(parser)
        cmd_options.relog_focus(parser, '')
        cmd_options.replay_options(parser)
        cmd_options.sdehome(parser, '')
        cmd_options.sim_add_filename(parser)
        cmd_options.wp_relog_dir(parser)

    def ParseCommandLine(self):
        """
        Process command line arguments and ensure they are valid.

        @return List of command line options
        """

        # import pdb ; pdb.set_trace()
        version = '$Revision: 1.78 $'
        version = version.replace('$Revision: ', '')
        ver = version.replace(' $', '')
        us = '%prog [options]\nVersion: ' + ver
        desc = 'Replays one, or more, pinball(s). Must use one of '\
               'the following options: \n'\
               '--replay_file, --all_file, --all_dir'

        util.CheckNonPrintChar(sys.argv)
        parser = optparse.OptionParser(usage=us, version=ver, description=desc)

        # Define the command line options which control the behavior of the
        # script.  Some of these methods take a 2nd argument which is the empty
        # string ''. If the script uses option groups, then this parameter is
        # the group. However, this script does not use option groups, so the
        # argument is empty.
        #
        cmd_options.debug(parser)
        cmd_options.verbose(parser)
        cmd_options.all_dir(parser)
        cmd_options.all_file(parser)
        cmd_options.config_file(parser)
        cmd_options.global_file(parser)
        cmd_options.list(parser, '')
        cmd_options.no_glob(parser)
        cmd_options.num_cores(parser, '')
        cmd_options.replay_dir(parser)
        cmd_options.replay_file(parser)
        cmd_options.replay_filter(parser)
        cmd_options.save_global(parser)

        self.AddAdditionalOptions(parser)

        # import pdb ; pdb.set_trace()
        (options, args) = parser.parse_args()

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        # Read in configuration files and set global variables.
        # No need to read in a config file.
        #
        # import pdb;  pdb.set_trace()
        config_obj = config.ConfigClass()
        config_obj.GetCfgGlobals(options,
                                 False)  # Don't need to require 4 variables

        # Once the tracing configuration parameters are read, get the kit in
        # case pinplayhome was set on the command line.
        #
        self.kit_obj = self.GetKit()

        # Print out the version number
        #
        # import pdb;  pdb.set_trace()
        if config.debug:
            print os.path.basename(sys.argv[0]) + " $Revision: 1.78 $"

        # Some error checking.
        #
        if options.replay_file == '' and options.all_dir == '' and options.all_file == '' and \
                options.replay_dir == '':
            msg.PrintAndExit(
                "Either a replay directory or replay file must be specified!")

        elif (options.all_dir != '' and options.all_file != '') or  \
             (options.all_dir != '' and options.replay_file != '') or \
             (options.all_file != '' and options.replay_file != ''):

            msg.PrintAndExit(
                "Specify either a replay directory or a replay file, not both!")

        return options

    def Replay(self, param, dirname, filename):
        """
        Replay a single pinball given the command line options and the name of
        the pinball to replay. It formats the appropriate command line options,
        saves global variables in a pickle file & calls the replayer script.

        @param param    Dictionary containing all parameters that need to be
                        passed into the method.  Need a dictionary because this
                        method is sometimes called by walk_callback() which
                        only allows one parameter in the functions it calls.
        @param dirname  Directory where pinball is located
        @param filename Pinball base file name

        @return Exit code from the replayer script.
        """

        if param.has_key('options'):
            options = param['options']
        else:
            msg.PrintAndExit(
                'method replay_dir.Replay() failed to get param \'options\'')
        if options.verbose:
            msg.PrintMsg('Start of Replay() in replay_dir.py')
        # import pdb ; pdb.set_trace()
        basename_file = os.path.join(dirname, filename)
        command = self.replayer_cmd + ' --replay_file ' + basename_file

        if options.verbose:
            msg.PrintMsg("-> Replaying pinball \"" + basename_file + "\"")
        if options.replay_options:
            command += ' --replay_options "' + options.replay_options + '"'

        # Check to see if need to add options for BB vector generation.  Set
        # 'log_opt' to any options the user may have put on the command line.
        #
        log_opt = options.log_options
        if options.bb_add_filename:
            file_name = os.path.basename(basename_file)

            # If there is a focus thread, then need to remove the TID from the
            # file name.
            #
            # NOTE: This code may need to be fixed when a method of running
            # Simpoints on all threads of cooperative pinballs is implemented.
            #
            file_name = util.RemoveTID(file_name)

            # Write BB vector files to the newly created *.Data directory.
            #
            data_dir = file_name + '.Data'
            if not os.path.isdir(data_dir):
                os.mkdir(data_dir)
            log_opt += ' -o ' + os.path.join(data_dir, file_name)

        # Check to see if need to add options when running a simulator.
        #
        # import pdb ; pdb.set_trace()
        if options.sim_add_filename:

            # Need to instantiate a kit of the type simulator being used.
            # This is required on order to get some kit specific information.
            #
            # NOTE: If you are adding a kit for a new simulator, then you need
            # to modify this code.
            #
            if config.sim_kit_type == config.BRPRED:
                import brpred_kit
                sim_kit = brpred_kit.BrPredKit()
            elif config.sim_kit_type == config.CMPSIM:
                import sde_cmpsim_kit
                sim_kit = sde_cmpsim_kit.CMPsimKit()
            else:
                msg.PrintAndExit('Undefined kit type in method replay_dir.Replay(): ' + \
                    str(config.sim_kit_type))

            # Add the simulator knob to specify the file for the output from
            # the simulator.
            #
            log_opt += ' ' + sim_kit.GetSimOutputFile(basename_file)

        # When 'log_opt' is added to the command line below, it will
        # put double quotes (") around all the options. Therefore, need to
        # remove any exising double quotes in the current value for the
        # string 'log_opt'.
        #
        log_opt = log_opt.replace('"', '')

        # If relogging WP pinballs, need to add the -log:basename knob with
        # the relogged pinball path/name. 
        #
        # import pdb ; pdb.set_trace()
        if options.wp_relog_dir:

            ft = util.GetFocusThreadPB(basename_file)
            if ft > -1 and not options.relog_focus:

                # If WP pinballs were relogged with a focus thread, then the
                # resulting pinballs were 'per thread', not 'cooperative'.  If
                # relogging with a different filter (i.e. options.relog_focus ==
                # False) then need to remove TID from base file name given to
                # the knob -log:basename.  
                #
                file_name = os.path.basename(util.RemoveTID(basename_file))
            else:
                file_name = os.path.basename(basename_file)

            log_opt += ' -log:basename ' + os.path.join(options.wp_relog_dir,
                                                        file_name)
            if not options.list:
                msg.PrintMsgDate('Relog whole program pinball: ' + file_name)

        if log_opt:
            command += ' --log_options "' + log_opt + '"'
        if options.playout:
            command += ' --playout '

        # if not options.list:
        #     msg.PrintMsg(command)

        # If not just listing the command, then dump the global
        # variables so the next Python script can have access to them.
        # Then run the script.
        #
        result = 0
        if not options.list:

            # Dump the global data to a unique file name.  Need to add the
            # option --global_file with this unique file name to options when
            # calling a script.
            #
            gv = config.GlobalVar()
            command += util.AddGlobalFile(gv.DumpGlobalVars(), options)
            command += util.AddCfgFile(options)

            result = util.RunCmd(command, options, filename,
                                 concurrent=True)  # Run concurrent jobs here

        else:
            # If the option 'list' is defined, then just list out the
            # commands to be exectuted, but don't execute them.
            #
            msg.PrintMsg(command)

        return result

    def ReplayOne(self, options):
        """
        Replays one set of pinballs. Calls Replay() with the pinball name.

        @param options Options given on cmd line

        @return exit code from replaying pinball
        """

        param = {'options': options}
        self.Replay(param, '', options.replay_file)
        if options.verbose:
            msg.PrintMsg('Calling WaitJobs() from replay_dir.py, ReplayOne()')
        result = util.WaitJobs(options)
        return result

    def ReplayAllFile(self, options):
        """Replays all pinballs based on a partial file name.

        @param options Options given on cmd line

        @return exit code from replaying all pinballs
        """

        if config.debug:
            print 'Replaying all pinballs matching patern.'

        import glob

        # If the user has given the "--all_file" option, then try to find all files
        # with this string. Replay all the pinballs in these directories.
        #
        result = 0
        if options.all_file:
            param = {'options': options}
            for path in glob.glob('*' + options.all_file + '*'):
                pos = path.find(".address")
                if pos != -1:
                    basename_file = path[0:pos]
                    self.Replay(param, '', basename_file)

            # Wait until all the jobs complete.
            #
            if options.verbose:
                msg.PrintMsg(
                    'Calling WaitJobs() from replay_dir.py, ReplayAllFile()')
            result = util.WaitJobs(options)

        return result

    def ProcessCommandLine(self, options):
        """
        Replay pinballs using one of the 4 possible methods.
              1) Replay one pinball
              2) Replay all pinballs names whose name contains a string
              3) Replay all pinballs in one directory
              4) Replay all pinballs in the directories whose name contains a string

        @param options Options given on cmd line

        @return Exit code from replaying the last pinball
        """

        # import pdb;  pdb.set_trace()
        result = 0
        if options.replay_file:
            result = self.ReplayOne(options)
        elif options.replay_dir:

            # Use util.RunAllDir() to execute the method Replay() on the
            # pinballs in the directory 'replay_dir'.  Pass no_glob = True so
            # the directory name is NOT expanded.
            #
            param = {'options': options}
            if options.verbose:
                msg.PrintMsg(
                    'Calling util.RunAllDir() from replay_dir.py, ProcessCommandLine() replay_dir')
            result = util.RunAllDir(options.replay_dir, self.Replay, True, param)
            if result != 0:
                return result

            # Once all the jobs are started, then wait for them to finish
            # before we proceed.
            #
            if options.verbose:
                msg.PrintMsg(
                    'Calling WaitJobs() from replay_dir.py, ProcessCommandLine() replay_dir')
            result = util.WaitJobs(options)
        elif options.all_dir:

            # Use util.RunAllDir() to execute the method Replay() on the
            # pinballs in the directories which contain the string in
            # options.alldir.  Pass no_glob = False so the directory name IS
            # expanded.
            #
            # import pdb;  pdb.set_trace()
            param = {'options': options}
            if options.verbose:
                msg.PrintMsg(
                    'Calling util.RunAllDir() from replay_dir.py, ProcessCommandLine() all_dir')
            result = util.RunAllDir(options.all_dir, self.Replay, False, param)
            if result != 0:
                return result

            # Once all the jobs are started, then wait for them to finish
            # before we proceed.
            #
            if options.verbose:
                msg.PrintMsg(
                    'Calling WaitJobs() from replay_dir.py, ProcessCommandLine() all_dir')
            result = util.WaitJobs(options)
        elif options.all_file:
            result = self.ReplayAllFile(options)
        else:
            result = -1
        return result

    def Run(self):
        """
        Get all the user options and replay all the desired pinballs.

        @return Exit code from replaying the last pinball
        """

        # Debugger will run when this is executed
        # import pdb;  pdb.set_trace()

        #import pdb ; pdb.set_trace()
        options = self.ParseCommandLine()
        result = self.ProcessCommandLine(options)

        # Clean up any global data files which still exist.
        #
        gv = config.GlobalVar()
        gv.RmGlobalFiles(options)

        return result


def main():
    """
    Process command line arguments and run the replayer

    @return Exit code from running the script
    """

    replay = ReplayMulti()
    result = replay.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
