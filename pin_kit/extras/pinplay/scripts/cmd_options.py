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
# $Id: cmd_options.py,v 1.100 2016/01/15 23:01:16 tmstall Exp tmstall $
"""
@package cmd_options

Define script command line options for use with module optparse

Here's the process for adding new options:

1) Determine the appropriate 'options group' for the new option, if any.

2) Add the option to this file.

3) Options to the top level and mid level scripts usually need to be propogated to
   other, lower level, scripts.  If so, then the following additional steps
   are required in the module 'config.py'.

   a) Add a new parameter in the section 'Tracing configuration parameters'.
   b) Add new parameter to these methods: DumpGlobalVars(), ReadGlobalVars()
      and SetGlobalVars().

4) If the new option is a parameter which can be read from a tracing
   parameter configuration file, then also add it to the method ParseCfgFile().
"""

import optparse
import re


class BlankLinesIndentedHelpFormatter(optparse.IndentedHelpFormatter):
    """
    Overload a method in class OptionParse which allows the programmer to
    insert either one or two blank lines in the help output.
    """

    def format_description(self, description):
        """
        Replace the two new lines '\n\n' (i.e. <CR><CR>) with special string and split
        description string by '\n' (i.e. <CR>).

        @param description String to be formatted

        @return string with formatted description
        """

        double_nl = description.replace('\n\n', '<CrcR>\n')
        blocks = double_nl.split('\n')

        # For each block, use the default format for the individual blocks
        # of text.
        #
        rets = []
        for block in blocks:
            rets.append(
                optparse.IndentedHelpFormatter.format_description(self, block))

        # Now replace the special string with the original string '\n\n'.
        #
        ret = "".join(rets)
        ret = ret.replace('<CrcR>\n', '\n\n')

        return ret

#########################################################################
#
# Callback methods to parse the arguments for "complicated" options.
#
#########################################################################


# Option.ALWAYS_TYPED_ACTIONS += ('callback',)
def CallbackString(option, opt_str, value, parser):
    """
    Call back used for options which have multiple strings.
    Put them into options as strings.

    @return no return
    """

    args = []
    num_found = 0
    # import pdb;  pdb.set_trace()
    for arg in parser.rargs:
        if arg[0] != "-":
            # A string was found without a leading '-', assume it's a string.
            # Add it to the list of strings found.
            #
            args.append(arg)
            num_found += 1
        else:
            # A string with a leading '-' was found, assume it's the next option,
            # not a string. Exit this loop.
            #
            break

    # Add the string to the option.
    #
    #import pdb;  pdb.set_trace()
    new_string = ' '.join(args)  # Get the new value
    old_str = getattr(parser.values, option.dest)  # Get the old value
    if old_str:
        new_string = old_str + ' ' + new_string  # Combine them
    setattr(parser.values, option.dest, new_string)

    def PrintStrings():
        import msg

        if len(args) > 1:
            msg.PrintMsgNoCR('Found multiple strings: ')
            for f in args:
                msg.PrintMsgNoCR(f + ' ')
            msg.PrintMsg('')

    # As a debugging aid, print out the strings when there are more than one.
    #
    # PrintStrings()

    # Remove the strings from the arguments still to be parsed.
    #
    # import pdb;  pdb.set_trace()
    if args != []:
        del parser.rargs[:num_found]


def CallbackList(option, opt_str, value, parser):
    """
    Call back used for options which can have multiple instances.

    Allows multiple options to be specified by using multiple options '--cfg
    XX' as well as '--cfg XXX,YYY' and '--cfg "XXX YYY".  Add all of them to
    option as a list.

    @return no return
    """

    args = []
    num_found = 0
    for arg in parser.rargs:
        if arg[0] != "-":
            # A string was found without a leading '-', assume it's not an
            # option.  
            #
            # If the string contains white space or commas, the string is split
            # into spearate strings.  Each of the resultings strings is added to 
            # 'option'.
            #
            arg = re.split('\s|,', arg)
            args += arg
            num_found += 1
            break

    # If the variable option.dest (given in the add_option() method) is in the parser,
    # which it certainly should be, then add the list of string to this option.
    #
    # import pdb;  pdb.set_trace()
    if getattr(parser.values, option.dest):

        # Add the new options in 'args' to the list of current options.  Need
        # to make sure the options are in the correct order.
        #
        opts = getattr(parser.values, option.dest)
        opts.extend(args)
        args = opts

    setattr(parser.values, option.dest, args)

    def PrintStrings():
        import msg

        if len(args) > 1:
            msg.PrintMsgNoCR('Found config files:     ')
            for f in args:
                msg.PrintMsgNoCR(f + ' ')
            msg.PrintMsg('')

    # As a bugging aid, print out the strings when there are more than one.
    #
    PrintStrings()

    # Remove the strings from the arguments still to be parsed.
    #
    # import pdb;  pdb.set_trace()
    if args != []:
        del parser.rargs[:num_found]

#########################################################################
#
# Top level options which define the tracing phases.
#
#########################################################################


# import pdb;  pdb.set_trace()
def GetMethod(parser, group):
    """
    If group is defined, then return the method to add this option to the group of options.  Otherwise
    return a method which will add it to the 'generic' options.
    """

    if group == '':
        method = parser.add_option
    else:
        method = group.add_option
    return method


def PhaseGroup(parser):
    """Define the group for 'default' options."""

    group = optparse.OptionGroup(
        parser, "Phase options",
        "These options select the tracing phases to execute. "
        "Either one, or more, phase option must be given or the option '--default_phases' "
        "which runs all default phases. "
        "Must define parameters 'program_name' and 'input_name' for every phase.  "
        "The logging phase '-log' also requires 'command' and 'mode'.  "
        "Phases are executed in the order given below:")

    return group


def default_phases(parser, group):
    method = GetMethod(parser, group)
    method(
        "--default_phases",
        dest="default_phases",
        action="store_true",
        help="Always run these default phases: log, basic_block_vector, simpoint, "
        "and region_pinball.  When using some simulators one, or more, of these "
        "additional phases may also be run by default: whole_sim, "
        "region_sim, pred_error.  Additional phases are also run by default with SDE.")


def native_pure(parser, group):
    method = GetMethod(parser, group)
    method(
        "--native_pure",
        dest="native_pure",
        action="store_true",
        help=
        "Run the application on the HW, without Pin or Pintools (no logging).")


def log(parser, group):
    method = GetMethod(parser, group)
    method("-l", "--log",
           dest="log",
           action="store_true",
           help="Generate whole program pinballs for the application.  "
           "Required parameters: program_name, input_name, command, mode")


def basic_block_vector(parser, group):
    method = GetMethod(parser, group)
    method(
        "-b", "--basic_block_vector",
        dest="basic_block_vector",
        action="store_true",
        help="Generate basic block vectors (BBV) for whole program pinballs.")


def ldv(parser, group):
    method = GetMethod(parser, group)
    method(
        "--ldv",
        dest="ldv",
        action="store_true",
        default=False,
        help="Generate LRU Stack Distance vectors (LDV), as well as Basic Block Vectors (BBV), for "
        "the whole program pinballs.  Then use both BBVs and LDVs as input to Simpoint. "
        "NOTE: In order to use both BBV/LDV for Simpoint,  this option must be given for both "
        "the phase which generate BBVs (--basic_block_vector) and the one which runs Simpoint (--simpoint).  "
        "See --combine for more info on how to define the method used to combine the two vector.  "
        "Default scaling factor if the option --combine is not used: 0.5")


def cb_match(parser, group):
    method = GetMethod(parser, group)
    method(
        "--cb_match",
        dest="cb_match",
        default=False,
        action="store_true",
        help="Run the cross binary simpoints matcher.  Used for CBSP.")


def simpoint(parser, group):
    method = GetMethod(parser, group)
    method(
        "-s", "--simpoint",
        dest="simpoint",
        action="store_true",
        help="Run Simpoint using whole program pinball basic block vectors (BBV).")


def replay(parser, group):
    method = GetMethod(parser, group)
    method("-r", "--replay",
           dest="replay",
           action="store_true",
           help="Replay all whole program pinballs.")


def replay_region(parser, group):
    method = GetMethod(parser, group)
    method("-R", "--replay_region",
           dest="replay_region",
           action="store_true",
           help="Replay all region pinballs.")


def region_pinball(parser, group):
    method = GetMethod(parser, group)
    method("-p", "--region_pinball",
           dest="region_pinball",
           action="store_true",
           help="Relog whole program pinballs using representative regions "
           "from Simpoint to generate region pinballs.")


def whole_sim(parser, group):
    method = GetMethod(parser, group)
    method('-W', '--whole_sim',
           dest="whole_sim",
           action="store_true",
           help="Run the simulator on the whole program pinballs.")


def region_sim(parser, group):
    method = GetMethod(parser, group)
    method('-T', '--region_sim',
           dest="region_sim",
           action="store_true",
           help="Run the simulator on the region pinballs.")


def pred_error(parser, group):
    method = GetMethod(parser, group)
    method(
        "-c", "--pred_error", "--calc_func",
        dest="pred_error",
        action="store_true",
        help="Calculate the prediction error, using the metric of interest, for a set of "
        "representative regions.  Must have already generated simulator data, either using "
        "phases '--whole_sim' and '--region_sim' or the appropriate options for your "
        "simulator,  before running this phase.  ('--calc_func' is included for backward "
        "compatability.)")


def traceinfo(parser, group):
    method = GetMethod(parser, group)
    method("-t", "--traceinfo",
           dest="traceinfo",
           action="store_true",
           help="Generate traceinfo XML files.")


def lit_gen(parser, group):
    method = GetMethod(parser, group)
    method("-L", "--lit_gen",
           dest="lit_gen",
           action="store_true",
           help="Generate LIT and LMAT files.")


def verify(parser, group):
    method = GetMethod(parser, group)
    method(
        "-v", "--verify", "--only_verify",
        dest="verify",
        action="store_true",
        help=
        "Verify the LIT files are valid by running them thru a simulator (does not verify LMAT files).")


def imix_lit(parser, group):
    method = GetMethod(parser, group)
    method("--imix_lit",
           dest="imix_lit",
           action="store_true",
           help="Generate the instruction mix for the LIT files.")


def imix_region(parser, group):
    method = GetMethod(parser, group)
    method(
        "--imix_region",
        dest="imix_region",
        action="store_true",
        help="Generate the instruction mix for the region pinballs.  Includes instructions for: "
        "prolog, warmup, region and epilog.")


def imix_whole(parser, group):
    method = GetMethod(parser, group)
    method("--imix_whole",
           dest="imix_whole",
           action="store_true",
           help="Generate the instruction mix for the whole program pinballs.")

#########################################################################
#
# Top level options which are parameters used in tracing.
#
#########################################################################


def ParameterGroup(parser):
    """Define the group for options which are parameters."""

    group = optparse.OptionGroup(parser, "General parameter options", \
            "These options allow the user to define tracing parameters. They "
            "can also be given in the tracing configuration file(s). Command "
            "line options over-ride parameters defined in the configuration "
            "file(s).   IMPORTANT NOTE: When using SDE (not PinPlay), the "
            "two parameters 'program_name' and 'input_name' can NOT have "
            "either the char '.' " "or '_' in them.")

    return group


def cbsp_name(parser, group):
    method = GetMethod(parser, group)
    method(
        "--cbsp_name",
        dest="cbsp_name",
        default="",
        metavar="CBSP_NAME",
        help="CBSP_NAME is the name of the Cross Binary SimPoint (CBSP) run.  This parameter is "
        "required for running CBSP.")


def command(parser, group):
    method = GetMethod(parser, group)
    method(
        "--command",
        dest="command",
        default="",
        metavar="COMMAND",
        help="COMMAND is the command line used to run the application being traced (binary "
        "and all required options). Must use \" to bracket the command if it contains "
        "more than one string. For example: \"ls -l\". "
        "No default. Must be defined in either a tracing configuration file or this option.")


def compressed(parser, group):
    method = GetMethod(parser, group)
    method(
        "--compressed",
        dest="compressed",
        default="bzip2",
        metavar="COMP",
        help=
        "COMP specifies which compression mode to be used (none, bzip2, gzip).  Default: bzip2")


def input_name(parser, group):
    method = GetMethod(parser, group)
    method(
        "--input_name",
        dest="input_name",
        default="",
        help="Name of the input file or workload. No default. Must be defined in either the "
        "tracing configuration file or this option.  "
        "NOTE: This paramater can NOT have either the char '.' or '_' in it.")


def focus_thread(parser, group):
    method = GetMethod(parser, group)
    method(
        "-f", "--focus_thread",
        dest="focus_thread",
        default=-1,
        help="Thread to use when running Simpoint & generating region pinballs for multi-thread apps. "
        "Default: 0.")


def mode(parser, group):
    method = GetMethod(parser, group)
    method("--mode",
           dest="mode",
           default="",
           metavar="MODE",
           choices=['st', 'mt', 'mpi', 'mpi_mt', 'mp', 'mp_mt', ''],
           help="MODE specifies the type of program to be logged. "
           "No default. Must be defined in either a tracing configuration "
           "file or this option.                                 "
           "st - single-threaded                                 "
           "mt - multi-threaded                                  "
           "mpi - MPI single-threaded                            "
           "mpi_mt - MPI multi-threaded                          "
           "mp - multi-process, single-threaded                  "
           "mp_mt - multi-process, multi-threaded                ")


def num_cores(parser, group):
    method = GetMethod(parser, group)
    method(
        "--num_cores",
        dest="num_cores",
        type="int",
        default="0",
        help="Number of cores to use for running phases concurrently.  Default is "
        "the number of cores on the system.  Set to 1 to run serially.")


def num_proc(parser, group):
    method = GetMethod(parser, group)
    method("-n", "--num_proc",
           dest="num_proc",
           type="int",
           default="0",
           help="Number of processes. Default: 1")


def pinplayhome(parser, group):
    method = GetMethod(parser, group)
    method(
        "--pinplayhome",
        dest="pinplayhome",
        default="",
        help=
        "Set the directory where the the PinPlay kit is located. Default: $HOME/pinplay")


def sdehome(parser, group):
    method = GetMethod(parser, group)
    method(
        "--sdehome",
        dest="sdehome",
        default="",
        help="Set the directory where the SDE kit is located. Default: $HOME/SDE")


def program_name(parser, group):
    method = GetMethod(parser, group)
    method(
        "--program_name",
        dest="program_name",
        default="",
        help="Name of the application to trace. No default. Must be defined in either the "
        "tracing configuration file or this option. "
        "NOTE: This paramater can NOT have either the char '.' or '_' in it.")

#########################################################################
#
# Simpoint phase options
#
#########################################################################


def SimpointPhaseGroup(parser):
    """Define the group for options which apply to Simponts."""

    group = optparse.OptionGroup(
        parser, "Simpoint parameter options",
        "These options define parameters which are used in the Simpoint phase (--simpoint).")
    return group


def cutoff(parser, group):
    method = GetMethod(parser, group)
    method(
        "--cutoff",
        dest="cutoff",
        default="1.0",
        type="float",
        help="Value which defines the fraction of the representative regions to use when generating "
        "region pinballs.                (0.0 > CUTOFF <= 1.0)  Default: 1.0")


def maxk(parser, group):
    """Note: Default values are NOT set here. They are set in the high level scripts."""

    method = GetMethod(parser, group)
    method(
        "--maxk",
        dest="maxk",
        default="0",
        type="int",
        help="Set the value MAXK for Simpoint. This is the maximum number of traces which may be "
        "generated.  Fewer traces will be generated in many cases. Default: 20")


def slice_size(parser, group):
    """Note: Default values are NOT set here. They are set in the high level scripts."""

    method = GetMethod(parser, group)
    method("-S", "--slice_size",
           dest="slice_size",
           default="0",
           type="int",
           help="Number of instructions in each slice (representative region). "
           "Default: 30,000,000")


def simpoint_options(parser, group):
    """Note: Default values are NOT set here. They are set in the high level scripts."""

    method = GetMethod(parser, group)
    method(
        "--simpoint_options",
        dest="simpoint_options",
        default="",

        help="Options passed to the Simpoint binary. NOTE: Replaces all knobs "
        "normally given on the Simpoint command line.  Must explicitly "
        "define all knobs required for the desired behavior when running "
        "Simpoints with this option.")

#########################################################################
#
# Region pinball generation phase options
#
#########################################################################


def RegionPBPhaseGroup(parser):
    """Define the group for options which apply to generating region pinballs."""

    group = optparse.OptionGroup(
        parser, "Region pinball generation parameter options",
        "These options define parameters which are used in the region pinball generation phase "
        "(--region_pinball).")
    return group


def epilog_length(parser, group):
    method = GetMethod(parser, group)
    method(
        "--epilog_length",
        dest="epilog_length",
        default="0",
        type="int",
        help="Number of extra instruction to be included after region. Default: 0")


def prolog_length(parser, group):
    method = GetMethod(parser, group)
    method("--prolog_length",
           dest="prolog_length",
           default="0",
           type="int",
           help="Number of extra instruction to be included before the region. "
           "Default: 0")


def warmup_length(parser, group):
    """Note: Default values are NOT set here. They are set in the high level scripts."""

    method = GetMethod(parser, group)
    method("-w", "--warmup_length",
           dest="warmup_length",
           default="0",
           type="int",
           help="Number of extra instruction to be included before prolog. "
           "Default: 500,000,000")

def warmup_factor(parser, group):
    method = GetMethod(parser, group)
    method( "--warmup_factor",
           dest="warmup_factor",
           default="0",
           type="int",
           help="Number of extra slices to be included before simulation pcregion. "
           "Default: 0")


def pccount_regions(parser, group):
    method = GetMethod(parser, group)
    method(
        "--pccount_regions",
        dest="pccount_regions",
        action="store_true",
        default=False,
        help="Describe regions with PC+count markers.")

def global_regions(parser, group):
    method = GetMethod(parser, group)
    method(
        "--global_regions",
        dest="global_regions",
        action="store_true",
        default=False,
        help="Describe regions using a single global profile for all threads.")

#########################################################################
#
# LIT file verification phase options
#
#########################################################################


def VerifyLITPhaseGroup(parser):
    """Define the group for options which apply to verifying LIT files."""

    group = optparse.OptionGroup(
        parser, "LIT file verification parameter options",
        "These options define parameters which are used in the phase to verify LIT files"
        " (--verify).")
    return group


def archsim_cf_file(parser, group):
    method = GetMethod(parser, group)
    method("--archsim_cf_file",
           dest="archsim_cf_file",
           default='',
           help="Archsim configuration file.")


def archsim_config_dir(parser, group):
    method = GetMethod(parser, group)
    method("--archsim_config_dir",
           dest="archsim_config_dir",
           default='',
           help="Directory with archsim configuration files.  Default: "
           "$HOME/sims/latest_archsim/skl-archsim-def-files/ww47-2011")


def simhome(parser, group):
    method = GetMethod(parser, group)
    method(
        "--simhome",
        dest="simhome",
        default="",
        help=
        "Set the directory where the simulator is located. Default: $HOME/sims/latest_keiko")


def sim_options(parser, group):
    method = GetMethod(parser, group)
    method("--sim_options",
           dest="sim_options",
           default='',
           help="Options passed to the simulator.")


def processor(parser, group):
    method = GetMethod(parser, group)
    method(
        '--processor',
        dest='processor',
        default='',
        help=
        'Target processor (or simulator configuration file) for the binary being traced/simulated.')

#########################################################################
#
# Top level options which modify the behavior of a phase.
#
#########################################################################


def ModifyGroup(parser):
    """Define the group for options used to modify the behavior of the phases."""

    group = optparse.OptionGroup(
        parser, "Phase modification options",
        "These options modify the behavior of one, or more, phase(s). ")

    return group


def coop_lit(parser, group):
    method = GetMethod(parser, group)
    method("--coop_lit",
           dest="coop_lit",
           action="store_true",
           default=False,
           help="Generate cooperative LIT files, not per thread, the default.")


def coop_pinball(parser, group):
    method = GetMethod(parser, group)
    method(
        "--coop_pinball",
        dest="coop_pinball",
        action="store_true",
        default=False,
        help=
        "Relog the region pinballs as cooperative, not per thread, the default.")


def cross_os(parser, group):
    method = GetMethod(parser, group)
    method(
        "--cross_os",
        dest="cross_os",
        action="store_true",
        default=False,
        help="Use when a log file (pinball) is collected on one OS, and processed "
        "on a different OS (e.g. Windows/Linux or Android/Linux).  This "
        "may cause some address ranges to be relocated and the translation of "
        "addresses referencing them.")


def mpi_options(parser, group):
    method = GetMethod(parser, group)
    method(
        "--mpi_options",
        dest="mpi_options",
        default="",
        help="Defines the MPI environment variables used to run the application.  "
        "Default values for these variables are:  "
        "                                                                                       "
        "-n num_proc "
        "                                                                                       "
        "-env I_MPI_DEVICE shm "
        "                                                                                       "
        "-env I_MPI_SPIN_COUNT 2147483647 "
        "                                                                                       "
        "-env I_MPI_PIN_MODE lib "
        "                                                                                       "
        "-env I_MPI_PIN_PROCS 3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12 "
        "                                                                                       "
        "-env I_MPI_DEBUG 4 ")


def native_pin(parser, group):
    method = GetMethod(parser, group)
    method(
        "--native_pin",
        dest="native_pin",
        action="store_true",
        help="Run the application using Pin, but with no logging. Used for native runs "
        "of binaries which require SDE to emulate instructions.")


def no_focus_thread(parser, group):
    method = GetMethod(parser, group)
    method("--no_focus_thread",
           dest="no_focus_thread",
           action="store_true",
           default=False,
           help="Generate region pinballs without a focus thread.")


def spec(parser, group):
    method = GetMethod(parser, group)
    method(
        "--spec",
        dest="spec",
        action="store_true",
        default=False,
        help="Do not check for special chars '_' and '.' when using SDE.  Useful for "
        "the SPEC benchmarks.  Default: (SDE only) check for special char and exit if they are found.")


def whole_pgm_dir(parser, group):
    """Note: Default value is NOT set here. It is set in config.py module."""

    method = GetMethod(parser, group)
    method(
        "--whole_pgm_dir",
        dest="whole_pgm_dir",
        default="",
        help="Use WHOLE_PGM_DIR as the directory for the whole program pinballs. "
        "This option is 'sticky'. That means, once the option is used to define "
        "the whole program directory, this dir name will always be used any time "
        "the tracing script is invoked with the current 'tracing instance'.  "
        "(Tracing instance as defined by:  program_name and input_name)  "
        "The default dir name when this option is NOT used is: whole_program.input_name.")

#########################################################################
#
# Relogging whole program phase filter options
#
#########################################################################


def WPFilterGroup(parser):
    """Define the group for options which apply filters to relogging of whole program pinballs."""

    group = optparse.OptionGroup(
        parser, "Whole program pinball filtering phases and options (relogging)",
        "These options define phases which are used to 'filter' the whole program pinballs "
        "during relogging. When multiple filters are selected, they are applied in the order "
        "the options are listed below. "
        "There are two types of options listed here: 'relog_*' and 'use_relog_*'. \n\n"
        ""
        "The first type of option (relog_*) define phases which modify WP pinballs by relogging with a set of "
        "knobs which 'filter out' various type of instructions.  For example, filter on a focus thread "
        "or remove initialization instructions using SSC marks.  "
        "The relog_* options are 'sticky' options.  That means, once they are used on a command "
        "line, the tracing environment will remember these options.  The next time you run the "
        "script, it will use the filtered whole program pinballs generated "
        "using the relog_* options. \n\n"
        ""
        "The 2nd type of option (use_relog_*) are used to select a set of filtered WP pinballs "
        "for the current invocation of the script.  The use_relog_* options "
        "on the current command line are used to select the set of filtered pinballs for this "
        "run instead of using the default set of filtered WP pinballs. This allows the user to "
        "over-ride a set of 'sticky' relog_* options.  Once you apply a set of use_relog_* options, these "
        "filters are used as the new 'sticky' filtering options.")

    return group


def relog_code_exclude(parser, group):
    method = GetMethod(parser, group)
    method(
        "--relog_code_exclude",
        dest="relog_code_exclude",
        default="",
        help="Relog the whole program pinballs excluding (filtering out) all instructions between two addresses. "
        "RELOG_CODE_EXCLUDE is a file which defines the instructions to be excluded. "
        "Each line in the file contains two addresses. "
        "The first address is the initial instruction to be excluded. "
        "The second is the address of the first instruction which will be included in the log file. "
        "(I.E. instructions are skipped between the 1st address and the instruction BEFORE the 2nd address.) "
        "Multiple sets of address are allowed, one set per line. "
        "The relogged WP pinballs will be in a directory "
        "which uses the original WP directory name with the string "
        "'[.relog].code_ex-RELOG_CODE_EXCLUDE' added to the name. ")


def relog_focus(parser, group):
    method = GetMethod(parser, group)
    method(
        "--relog_focus",
        dest="relog_focus",
        action="store_true",
        default=False,
        help="Relog the whole program pinballs using the current focus thread. "
        "The relogged WP pinballs will be in a directory "
        "which uses the original WP directory name with the string '[.relog].per_thread_X' added to the name. "
        "(Where X is the focus thread.)")


def relog_name(parser, group):
    method = GetMethod(parser, group)
    method(
        "--relog_name",
        dest="relog_name",
        default="",
        help="Relog the whole program pinballs.  The user must define the knobs used to control "
        "the relogging step with the option '--log_options'. "
        "The relogged WP pinballs will be in a directory "
        "which uses the original WP directory name with the string '[.relog].RELOG_NAME' added to the name.")


def relog_no_cleanup(parser, group):
    method = GetMethod(parser, group)
    method(
        "--relog_no_cleanup",
        dest="relog_no_cleanup",
        action="store_true",
        default=False,
        help="Relog the whole program pinballs and remove the cleanup instructions (after iterative application code) "
        "between the appropriate SSC marks.  The relogged WP pinballs will be in a directory "
        "which uses the original WP directory name with the string '[.relog].no_cleanup' added to the name.")


def relog_no_init(parser, group):
    method = GetMethod(parser, group)
    method(
        "--relog_no_init",
        dest="relog_no_init",
        action="store_true",
        default=False,
        help="Relog the whole program pinballs and remove the initialization instructions (before "
        "iterative application code) "
        "between the appropriate SSC marks.  The relogged WP pinballs will be in a directory "
        "which uses the original WP directory name with the string '[.relog].no_init' added to the name.")


def relog_no_omp_spin(parser, group):
    method = GetMethod(parser, group)
    method(
        "--relog_no_omp_spin",
        dest="relog_no_omp_spin",
        action="store_true",
        default=False,
        help="Relog the whole program pinballs and remove the OpenMP spin instructions. "
        "(Must use an Intel OpenMP library with SSC marks.)  The relogged WP pinballs will be in a directory "
        "which uses the original WP directory name with the string '[.relog].no_omp_spin' added to the name.")


def relog_no_mpi_spin(parser, group):
    method = GetMethod(parser, group)
    method(
        "--relog_no_mpi_spin",
        dest="relog_no_mpi_spin",
        action="store_true",
        default=False,
        help="Relog the whole program pinballs and remove the MPI spin instructions. "
        "(Must use an Intel MPI library with SSC marks.)  The relogged WP pinballs will be in a directory "
        "which uses the original WP directory name with the string '[.relog].no_mpi_spin' added to the name.")


def UseRelogOptionsSet(options):
    """
    Are any of the 'use_relog_*' options set to True?

    If new use_relog_* options are added, then make sure and
    add them to this method.
    """

    return options.use_relog_code_exclude != '' or options.use_relog_focus or \
        options.use_relog_name != '' or options.use_relog_no_cleanup or \
        options.use_relog_no_init or options.use_relog_no_omp_spin or \
        options.use_relog_no_mpi_spin


def RelogOptionsSet(options):
    """
    Are any of the 'relog_*' options set to True?

    If new relog_* options are added, then make sure and
    add them to this method.
    """

    return options.relog_code_exclude != '' or options.relog_focus or \
        options.relog_name != '' or options.relog_no_cleanup or \
        options.relog_no_init or options.relog_no_mpi_spin or \
        options.relog_no_mpi_spin


def use_relog_code_exclude(parser, group):
    method = GetMethod(parser, group)
    method(
        "--use_relog_code_exclude",
        dest="use_relog_code_exclude",
        default="",
        help="Use whole program pinballs relogged with code exclusion for subsequent phases. "
        "The string USE_RELOG_CODE_EXCLUDE is the extension of the whole program pinball directory defined "
        "by the user.")


def use_relog_focus(parser, group):
    method = GetMethod(parser, group)
    method(
        "--use_relog_focus",
        dest="use_relog_focus",
        default=False,
        action="store_true",
        help=
        "Use whole program pinballs relogged with a focus thread for subsequent phases.")


def use_relog_name(parser, group):
    method = GetMethod(parser, group)
    method(
        "--use_relog_name",
        dest="use_relog_name",
        default="",
        help="Use whole program pinballs relogged with a user defined name for subsequent phases.  "
        "The string USE_RELOG_NAME is the extension of the whole program pinball directory defined "
        "by the user.")


def use_relog_no_cleanup(parser, group):
    method = GetMethod(parser, group)
    method(
        "--use_relog_no_cleanup",
        dest="use_relog_no_cleanup",
        action="store_true",
        default=False,
        help="Use whole program pinballs which have been relogged to remove cleanup "
        "instructions.")


def use_relog_no_init(parser, group):
    method = GetMethod(parser, group)
    method(
        "--use_relog_no_init",
        dest="use_relog_no_init",
        action="store_true",
        default=False,
        help="Use whole program pinballs which have been relogged to remove initialization "
        "instructions.")


def use_relog_no_omp_spin(parser, group):
    method = GetMethod(parser, group)
    method(
        "--use_relog_no_omp_spin",
        dest="use_relog_no_omp_spin",
        action="store_true",
        default=False,
        help="Use whole program pinballs which have been relogged to remove OpenMP spin "
        "instructions.")


def use_relog_no_mpi_spin(parser, group):
    method = GetMethod(parser, group)
    method(
        "--use_relog_no_mpi_spin",
        dest="use_relog_no_mpi_spin",
        action="store_true",
        default=False,
        help="Use whole program pinballs which have been relogged to remove MPI spin "
        "instructions.")


def list(parser, group):
    method = GetMethod(parser, group)
    method(
        "--list",
        dest="list",
        action="store_true",
        default=False,
        help=
        "Print the list of commands this script would execute. Does NOT run these commands.")

#########################################################################
#
# Top level options for misc behaviors.
#
#########################################################################


def add_program_wp(parser):
    parser.add_option(
        "--add_program_wp",
        dest="add_program_wp",
        action="store_true",
        default=False,
        help="Add parameter 'program_name' to the directory name used for "
        "the whole program pinballs.")


def check_code_exclude(parser):
    parser.add_option(
        "--check_code_exclude",
        dest="check_code_exclude",
        default="",
        help="Run the code exclusion checker.  "
        "CHECK_CODE_EXCLUDE is a file which defines the instructions to be excluded. "
        "Each line in the file contains three addresses. "
        "1. Starting address of function to skip. "
        "2. Ending address of function to skip (largest region to be skipped). "
        "3. Starting address(s) of function(s) which calls the function to be skipped.  "
        "Instructions are skipped starting at the address in the function given in (1) "
        "and logging is started again after the last address in the function given in (2)is "
        "executed.  "
        "Multiple sets of address are allowed for (3).")


def delete(parser):
    parser.add_option(
        "--delete",
        dest="delete",
        action="store_true",
        help="CAUTION! This option will delete all the files and directories generated with "
        "the current tracing parameters.  The only exception is if the user has specified a "
        "whole program pinball directory with the option '--whole_pgm_dir', then it will NOT be "
        "deleted.")


def delete_wp(parser):
    parser.add_option(
        "--delete_wp",
        dest="delete_wp",
        action="store_true",
        help="CAUTION! This option will delete all the files and directories generated with "
        "the current tracing parameters, including the whole program pinball directory given by "
        "the option '--whole_pgm_dir'.")


def delete_all(parser):
    parser.add_option(
        "--delete_all",
        dest="delete_all",
        action="store_true",
        help=
        "CAUTION! Delete ALL files and directories generated for ALL tracing instances.")


def dir_separator(parser):
    parser.add_option(
        "--dir_separator",
        dest="dir_separator",
        default="",
        help="Use the char DIR_SEPARATOR as the separator between program_name and input_name "
        "in the Data/pp directory names.  Default char: '.'")


def global_file(parser):
    parser.add_option(
        "--global_file",
        dest="global_file",
        default="",
        help=
        "Name of the global data file used to communicate data between scripts.")

#########################################################################
#
# Options for 2nd level wrapper scripts and low level modules (log, replayer, record).
#
#########################################################################


def actual_metric(parser):
    parser.add_option(
        "--actual_metric",
        dest="actual_metric",
        default="",
        help="Get the metric of interest for the entire simulator run of the "
        "data files in directory ACTUAL_METRIC. (i.e. the 'measured' value of the metric for the run)")


def all_dir(parser):
    parser.add_option(
        "--all_dir",
        dest="all_dir",
        default='',
        help="Apply this action to all directory names that contain the string ALL_DIR. "
        "Ignores any dir/file specified with --replay_filter")


def all_file(parser):
    parser.add_option(
        "--all_file",
        dest="all_file",
        default='',
        help="Apply this action to all pinballs that contain the string ALL_FILE. "
        "Ignores any dir/file specified with --replay_filter")


def append_status(parser):
    parser.add_option("--append_status",
                      dest="append_status",
                      action="store_true",
                      default=False,
                      help="Append status information to the existing status file for this run "
                      "instead of overwriting it.  Default is to overwrite file.")


def attach(parser):
    parser.add_option("--attach",
                      dest="attach",
                      action="store_true",
                      default=False,
                      help="Attach to a running logger instance.")


def bbv_file(parser):
    parser.add_option("--bbv_file",
                      dest="bbv_file",
                      default='',
                      help="Basic block vector file to use for Simpoint.")

def bb_add_filename(parser):
    parser.add_option(
        "--bb_add_filename",
        dest="bb_add_filename",
        action="store_true",
        help="Add the base filename when generating basic block vectors.")


def sim_add_filename(parser):
    parser.add_option("--sim_add_filename",
                      dest="sim_add_filename",
                      action="store_true",
                      help="Add the base filename when running the simulator.")


def data_dir(parser):
    parser.add_option("--data_dir",
                      dest="data_dir",
                      default='',
                      help="Write the Simpoint data to directory DIR")


def wp_relog_dir(parser):
    parser.add_option(
        "--wp_relog_dir",
        dest="wp_relog_dir",
        default="",
        help="Add the base filename when generating basic block vectors.")


def config_file(parser):
    parser.add_option(
        "--cfg FILE", "--config_file FILE",
        dest="config_file",
        default="",
        action="callback",
        callback=CallbackList,
        help="Give one, or more, file(s) containing the application tracing parameters. "
        "Must use '--cfg' for each file.")


def cbsp_cfgs(parser):
    parser.add_option(
        "--cbsp_cfgs FILE,FILE[,FILE]",
        dest="cbsp_cfgs",
        default="",
        action="callback",
        callback=CallbackList,
        help="List two, or more, configuration file(s) which define the binaries "
        "to be used for Cross Binary SimPoints (CBSP).  Multiple instances of "
        "this option will be concatenated into one list of binaries.  This is "
        "required to run the CBSP script.")


def debug(parser):
    parser.add_option(
        "-d", "--debug",
        dest="debug",
        action="store_true",
        default=False,
        metavar="COMP",
        help="Print out the command(s), but do not execute.  Also prints out diagnostic "
        "information as well.")


def lit_options(parser):
    parser.add_option("--lit_options",
                      dest="lit_options",
                      default='',
                      help="Pass raw options to pinLIT pintool.")


def log_file(parser):
    parser.add_option(
        "--log_file", "--pinball",
        dest="log_file",
        default="",
        metavar="FILE",
        help="Use FILE as the log file (pinball) name. FILE can be basename of pinball "
        "or path+basename.")


def log_options(parser):
    parser.add_option("--log_options",
                      dest="log_options",
                      default="",
                      help="Pass knobs (options) to the logger pintool.")


def msgfile_ext(parser):
    parser.add_option(
        "--msgfile_ext",
        dest="msgfile_ext",
        default='',
        help="Use MSGFILE_EXT as the string used to describe the message file generated by "
        "the PinPlay tools.  This REPLACES the default description generated by the PinTool. For example, "
        "when replaying a pinball, and using this option with the string '.test', "
        "the msg file will end with the string '.test.txt' instead of 'replay.txt'.  NOTE: Use with caution "
        "as this same string will be used for the msg files in ALL phases which are executed.")


def no_glob(parser):
    parser.add_option(
        "--no_glob",
        dest="no_glob",
        action="store_true",
        default=False,
        help="Do not expand the directory name to include all directories which "
        "start with the string.  Just use the directory name 'as is'.")

def no_log(parser):
    parser.add_option(
        "--no_log",
        dest="no_log",
        action="store_true",
        default=False,
        help="Do not run the logger, just run the command with pin.")

def no_print_cmd(parser):
    parser.add_option("--no_print_cmd",
                      dest="no_print_cmd",
                      action="store_true",
                      default=False,
                      help="Do not print the pin command line.")


def pid(parser):
    parser.add_option(
        "--pid",
        dest="pid",
        type=int,
        default=None,
        help="Record a log file (pinball) for the processs given by PID")


def pintool(parser):
    parser.add_option(
        "--pintool",
        dest="pintool",
        default="",
        help="Override the default pintool for this kit with PINTOOL.")


def pintool_help(parser):
    parser.add_option(
        "--pintool_help",
        dest="pintool_help",
        action="store_true",
        default=False,
        help="Display the help msg for the specific pintool being used.")


def pin_options(parser):
    parser.add_option("--pin_options",
                      dest="pin_options",
                      default="",
                      help="Pass knobs (options) to pin (not the pintool).")


def playout(parser):
    parser.add_option(
        "--playout",
        dest="playout",
        action="store_true",
        default=False,
        help="Allows replay output to stdout and stderr to happen.")


def predicted_metric(parser):
    parser.add_option(
        "--predicted_metric",
        dest="predicted_metric",
        default="",
        help="Get the predicted metric of interest for the simulator run on just the "
        "representative regions of the data files in the directory PREDICTED_METRIC. (Does NOT "
        "include warmup instructions.)")


def replay_filter(parser):
    parser.add_option(
        "--replay_filter",
        dest="replay_filter",
        default="",
        help=
        "Filter out any file/dir which matches the string REPLAY_FILTER so it will not be replayed.")


def replay_dir(parser):
    parser.add_option(
        "--replay_dir",
        dest="replay_dir",
        default='',
        help="Apply this action to the specific directory name REPLAY_DIR. "
        "Ignores any dir/file specified with --replay_filter")


def replay_file(parser):
    parser.add_option(
        "--replay_file",
        dest="replay_file",
        default="",
        help="Replay pinball files with base file name REPLAY_FILE.")


def replay_options(parser):
    parser.add_option("--replay_options",
                      dest="replay_options",
                      default="",
                      help="Pass raw options to the replayer pintool.")


def save_global(parser):
    parser.add_option(
        "--save_global",
        dest="save_global",
        action="store_true",
        default=False,
        help=
        "To help debugging, save the global data files instead of deleting them.")


def simpoint_file(parser):
    parser.add_option("--simpoint_file",
                      dest="simpoint_file",
                      default="",
                      help="Name of Simpoint output file.")


def trace_basename(parser):
    parser.add_option("--trace_basename",
                      dest="trace_basename",
                      default="",
                      help="Basename of the trace.")


def verbose(parser):
    parser.add_option("--verbose",
                      dest="verbose",
                      action="store_true",
                      default=False,
                      help="Print out command line used and other information.")

#########################################################################
#
# Options for script: rename.py
#
#########################################################################


def RenameRequiredOpts(parser):
    """Define the group of required options for the scritpt 'rename.py'."""

    group = optparse.OptionGroup(
        parser, 'Required arguments',
        'This script requires the five following arguments in order '
        'to run. They can be defined either on the command line using options '
        'or with a configuration file or a mixture of the two methods.  '
        'Command line options over ride arguments defined in configuration files.\n\n'
        'NOTE: The strings used for the values of the arguments can NOT have either the '
        'char \'.\' or \'_\' in them.  This is a GTR requirement.  '
        'The script will fail if these characters are used.')
    return group


def app_version(parser, group):
    method = GetMethod(parser, group)
    method(
        '--app_version',
        dest='app_version',
        default='',
        help='Application version.  '
        'Must be defined in either a tracing configuration file or this option.  ')


def compiler_version(parser, group):
    method = GetMethod(parser, group)
    method(
        '--compiler_version',
        dest='compiler_version',
        default='',
        help='Version of the compiler used for the traced binary.  '
        'Must be defined in either a tracing configuration file or this option. ')


def platform(parser, group):
    method = GetMethod(parser, group)
    method(
        '--platform',
        dest='platform',
        default='',
        help='Information about the platform in one string with three components: ProcessorProcfreq-Busfreq '
        'Processor is three letter processor name (nhm, snb, skl).  ProcFreq is the processor frequency. '
        'BusFreq is QPI or bus freq without multiplier (6400 or 400).  For example: snb2800-3200.  '
        'Must be defined in either a tracing configuration file or this option.  ')


def date(parser, group):
    method = GetMethod(parser, group)
    method('--date',
           dest='date',
           default='',
           help='Date traces were generated using the format:  YYMM.  '
           'Over rides default which is the current year and month.')

#########################################################################
#
# Options for script: regions.py
#
#########################################################################


def ActionGroup(parser):
    """Define the group of 'actions' the script can execute."""

    group = optparse.OptionGroup(
        parser, "Script actions",
        "These options list the actions the scripts can perform.  "
        "One, and only one, action must be given when running the script.")
    return group


def combine(parser, group):
    """
    If combine not given by the user, it's set to -1.0 to indicate the user has
    not given a value.  The default scaling factor used for combining BBV/LDV
    if combine is not given on the command line is set in
    util.SetCombineDefault().
    """
    method = GetMethod(parser, group)
    method(
        "--combine",
        dest="combine",
        type=float,
        default=-1.0,
        help="When combining the vectors for BBV and LDV files into a single frequency vector file, "
        "use scaling factor COMBINE (0.0 >= COMBINE <= 1.0).  The BBV "
        "is scaled by COMBINE, while the LDV is scaled by 1-COMBINE.  Default scaling factor "
        "if this option is not used: 0.5")

def dimensions(parser, group):
    method = GetMethod(parser, group)
    method(
        "--dimensions",
        dest="dimensions",
        type=int,
        default=None,
        help="Set the number of dimensions to use for both BBV and LDV files.  "
        "Default dimension: 32")


def csv_region(parser, group):
    method = GetMethod(parser, group)
    method(
        "--csv_region",
        dest="csv_region",
        action="store_true",
        default=False,
        help="Generate a regions CSV file.  This was the default action for previous "
        "versions of this script (1.18 and earlier).  Must use --bbv_file, --region_file and "
        "--weight_file options to define files to process.")


def project_bbv(parser, group):
    method = GetMethod(parser, group)
    method(
        "--project_bbv",
        dest="project_bbv",
        action="store_true",
        default=False,
        help="Project an BBV file to only 15 dimensions using "
        "a random projection matrix.  Normalizes resulting vectors.  Must use option --bbv_file.")


def weight_ldv(parser, group):
    method = GetMethod(parser, group)
    method(
        "--weight_ldv",
        dest="weight_ldv",
        action="store_true",
        default=False,
        help="Apply appropriate weights to an LDV file.  Normalizes resulting vectors.  "
        "Must use option --ldv_file.")


def FileGroup(parser):
    """Define the group which defines all the types of files the script can process."""

    group = optparse.OptionGroup(
        parser, "File options",
        "These options define the types of files the script processes.  "
        "At least one file must be defined using these options when running the script.")
    return group


def bbv_file(parser, group):
    method = GetMethod(parser, group)
    method("--bbv_file",
           dest="bbv_file",
           default=None,
           help="Name of an BBV file")


def normal_bbv(parser, group):
    method = GetMethod(parser, group)
    method("--normal_bbv",
           dest="normal_bbv",
           default=None,
           help="Name of projected, normalized BBV file")


def normal_ldv(parser, group):
    method = GetMethod(parser, group)
    method("--normal_ldv",
           dest="normal_ldv",
           default=None,
           help="Name of an weighted, normalized LDV file")


def ldv_file(parser, group):
    method = GetMethod(parser, group)
    method("--ldv_file",
           dest="ldv_file",
           default=None,
           help="Name of an LDV file")


def region_file(parser, group):
    method = GetMethod(parser, group)
    method("--region_file",
           dest="region_file",
           default=None,
           help="Name of a file containing the Simpoint region information")


def vector_file(parser, group):
    method = GetMethod(parser, group)
    method(
        "--vector_file",
        dest="vector_file",
        default=None,
        help="Name of a text file containing a normalized frequency vector file")


def weight_file(parser, group):
    method = GetMethod(parser, group)
    method("--weight_file",
           dest="weight_file",
           default=None,
           help="Name of the file containing Simpoint weight information")

#########################################################################
#
# Options for DrDebug scripts
#
#########################################################################


def arch(parser, group):
    parser.add_option(
        "--arch",
        dest="arch",
        default='intel64',
        choices=['intel64', 'ia32'],
        help= "Architecture of the binary, either 'intel64' or 'ia32' (default: intel64)")


def debug_port(parser, group):
    parser.add_option(
        "--debug_port",
        dest="debug_port",
        default=None,
        help="Specify the port to use for communicating between GDB and Pin.  "
        "Default is to use the the port chosen by Pin.")


def gdb(parser, group):
    parser.add_option(
        "--gdb",
        dest="gdb",
        metavar="GDB_BINARY",
        default=None,
        help="GDB binary to use")


def gdb_options(parser, group):
    parser.add_option("--gdb_options",
        dest="gdb_options",
        default="",
        help="Pass options to GDB")


def mp_type(parser, group):
    parser.add_option(
        "--mp_type",
        dest="mp_type",
        default='None',
        choices=['mpi', 'mp', 'None'],
        help="Type of multiple process application (MPI or fork/exec).  Choose from 'mpi' or 'mp'.  "
        "If mp_type not given, assume application is a single process.")


def pintool_options(parser):
    """
    Define a 'generic' option for pintools instead of using either '--log_options' or '--replay_options'.
    This is done to try and avoid confusing the user of the DeDebug tools.
    """

    parser.add_option("--pintool_options",
        dest="pintool_options",
        default="",
        help="Pass knobs (options) to the pintool (not to pin)")


def single_thread(parser):
    parser.add_option(
        "--st", "--single_thread",
        dest="single_thread",
        action="store_true",
        default=False,
        help=
        "Application to record is a single-threaded app (default: multi-threaded apps)")
