#!/usr/bin/env python

# BEGIN_LEGAL
# BSD License
#
# Copyright (c)2013 Ghent University. All rights reserved.
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
# @ORIGINAL_AUTHORS: Trevor Carlson, T. Mack Stallcup, Cristiano Pereira, Harish Patil, Chuck Yount
#
# $Id: sniper_pinpoints.py,v 1.38 2015/08/15 19:58:48 tmstall Exp tmstall $$
#

# This is a script to replay one pinplay process
#

import sys
import os
import glob
import re
import locale

# Local modules
#
import cmd_options
import config
import msg
import pinpoints
import util


# Local utilities
def sniper_root(parser):
    parser.add_option(
        "--sniper_root",
        dest="sniper_root",
        default=os.getenv('SNIPER_ROOT', ''),
        help="Sniper root is the top level directory where Sniper is installed.  Defaults "
        "to the environment variable SNIPER_ROOT.")


def sniper_options(parser):
    parser.add_option(
        "--sniper_options",
        dest="sniper_options",
        default=False,
        help=
        "Replace the SniperLite configuration file with user defined config/options.")


def ignore_sniper_error(parser):
    parser.add_option("--ignore_sniper_error",
                      dest="ignore_sniper_error",
                      default=False,
                      action='store_true',
                      help="Ignore any erorrs in the Sniper output")


def no_sniperlite(parser):
    parser.add_option("--no_sniperlite",
                      dest="no_sniperlite",
                      default=False,
                      action='store_true',
                      help="Use Sniper instead of SniperLite as the simulator.")


def region_sim(parser, group):
    method = cmd_options.GetMethod(parser, group)
    method('-T', '--region_sim',
           dest="region_sim",
           action="store_true",
           help="Run Sniper on the region pinballs.")


def whole_sim(parser, group):
    method = cmd_options.GetMethod(parser, group)
    method('-W', '--whole_sim',
           dest="whole_sim",
           action="store_true",
           help="Run Sniper on the whole program pinballs.")


def RunSniper(pp_dir, sim_replay_cmd, phase_length, options):

    def round(x, roundby=1000000):
        return int(int((x / float(roundby))) * float(roundby))

    # List of pinballs used to print Sniper output when all runs are complete.
    #
    pb_list = []

    ret = 0
    if not os.path.isdir(pp_dir):

        # If running in MPI_MT_MODE, then it's possible for one process to not
        # have a thread corresponding to the the current focus thread.
        # However, another process might have this thread.  Thus, only return
        # an error if not tracing a MPI_MT application.
        #
        if options.mode == config.MPI_MT_MODE:
            msg.PrintMsg('WARNING: Directory containing pinballs to run with simulator does not exist:\n   ' + \
                pp_dir)
            msg.PrintMsg('Since tracing mode is \'mpi_mt\', this may be OK.')
            return 0
        else:
            msg.PrintMsg('ERROR: Directory containing pinballs to run with simulator does not exist:\n   ' + \
                pp_dir)
            return -1

    # List of output sniper directories.
    #
    output_dir_list = []

    for fn in os.listdir(pp_dir):
        if fn.endswith('.address'):
            pinball_path = os.path.join(pp_dir, os.path.splitext(fn)[0])
            fn = os.path.splitext(fn)[0]
            sniper_outputdir = os.path.join(config.sniper_result_dir,
                                            pinball_path)
            output_dir_list += [sniper_outputdir]
            sniper_outputfile = pinball_path + config.sniper_out_ext
            if options.debug:
                # If debugging, check to see if the Sniper result files already exist for the pinball.  If so,
                # then print it out but don't run Sniper again.
                #
                if os.path.isdir(sniper_outputdir):
                    msg.PrintMsgPlus(
                        'WARNING: Skipping Sniper execution because output file already exists.\n'
                        '   %s' % sniper_outputdir)
                    pb_list.append(pinball_path)
                    continue

            # Select the proper config/options to run the desired version of
            # Sniper/SniperLite.
            #
            # import pdb;  pdb.set_trace()
            if options.sniper_options:
                common_sniper_opts = options.sniper_options
            else:
                if options.no_sniperlite:
                    common_sniper_opts = ''
                else:
                    # use_orig = True        # Use older SniperLite options
                    use_orig = False  # New SniperLite options
                    if use_orig:
                        common_sniper_opts = ' -c dunnington -c cacheonly -c nehalem_cmpsim.cfg ' \
                      '-g --general/enable_icache_modeling=false ' \
                      '-g --perf_model/dram/direct_access=true ' \
                      '-g --perf_model/dram/queue_model/type=contention ' \
                      '-g --perf_model/dtlb/size=0'
                    else:
                        # Older patched Sniper 5.3
                        #
                        # common_sniper_opts = ' -c dunnington -c nehalem_cmpsim.cfg -c ccpp1c --pinball-non-sift \
                        # -g -replay:addr_trans -g --general/enable_icache_modeling=false'

                        # Newer patched Sniper 5.3
                        #
                        # common_sniper_opts = ' -c dunnington -c nehalem_cmpsim.cfg -c cc-fast --pinball-non-sift \
                        # -g -replay:addr_trans -g --general/enable_icache_modeling=false'

                        # Production SniperLite 6.0 options
                        #
                        common_sniper_opts = ' -c nehalem-lite --pinball-non-sift '
            partial_run_cmd = (common_sniper_opts + ' --no-cache-warming')

            try:
                # If re.search() fails, code falls though to the exception.
                #
                warmup_region = re.search('warmup(\d+)_prolog(\d+)_region(\d+)_epilog(\d+)_(\d+)_(\d-\d+)', \
                                          pinball_path)

                # Get info on the length of regions in the pinball.
                #
                warmup = int(warmup_region.group(1))
                prolog = int(warmup_region.group(2))
                file_region = int(warmup_region.group(3))
                epilog = int(warmup_region.group(4))
                region_num = int(warmup_region.group(5))
                weight = warmup_region.group(6).replace('-', '.')

                icount = util.GetMaxIcount('', pinball_path)
                sim_region_len = icount - warmup
                calc_region = sim_region_len - prolog - epilog

                if warmup != 0:
                    # If there are warmups, then need to use options to first do cache warmups in Sniper,
                    # then simulate the region.
                    #
                    partial_run_cmd = (common_sniper_opts + ' -s stop-by-icount:%d:%d ' \
              % (sim_region_len, round(warmup)) + ' --roi-script ')

                if not options.list:
                    # Print details about the various sections in the pinball.
                    #
                    msg.PrintMsgPlus('Running Sniper on: ' + fn)
                    msg.PrintMsg('           Warmup count:        ' +
                                 locale.format('%14d', warmup, True))
                    msg.PrintMsg('           Prolog count:        ' +
                                 locale.format('%14d', prolog, True))
                    msg.PrintMsg('           Actual region count: ' + locale.format('%14d', calc_region, True) + \
                        '   (from file name: ' + locale.format('%d', file_region, True) + ')')
                    msg.PrintMsg('           Epilog count:        ' +
                                 locale.format('%14d', epilog, True))
                    msg.PrintMsg('           Total Instr count:   ' +
                                 locale.format('%14d', icount, True))

            except AttributeError:
                if 'whole_program' not in pinball_path:

                    # Whole program pinballs cannot be processed using the options
                    # given above (using -s stop-by-icount:) because they don't have
                    # warmup instructions to be skipped.   
                    #
                    # Let the user know the results may be suspect because the dir
                    # appears to contain whole program pinballs, but the name doesn't
                    # contain the string 'whole_program'.  However, don't flag this as
                    # an erorr.  It's possible for the user to give the name of a WP
                    # pinball directory which does not contain this string.
                    #
                    msg.PrintMsgPlus(
                        'WARNING: This pinball may not really be a whole program pinball.\n'
                        '     If this is true, the results may not be valid.\n'
                        '         ' + pinball_path)
                msg.PrintMsgPlus(
                    'Running Sniper on whole program pinball: ' + pinball_path)

            # Format the command and execute it asynchronously.
            #
            cmd = os.path.join(options.sniper_root, sim_replay_cmd) + partial_run_cmd + \
                      (' -d "%s" ' % sniper_outputdir) + ' --pinballs ' + pinball_path + \
                       ' > ' + sniper_outputfile + ' 2>&1 '
            pb_list.append(pinball_path)
            end_str = fn
            # import pdb;  pdb.set_trace()
            result = util.RunCmd(cmd, options, end_str, concurrent=True)
            ret = result or ret
    result = util.WaitJobs(options)
    ret = result or ret

    # Make sure some of the important Sniper output files exist for each pinball.
    #
    file_list = ['sim.stats.sqlite3', 'sim.info', 'sim.cfg', 'sim.out']
    for sim_dir in output_dir_list:
        for f in file_list:
            if not os.path.isfile(os.path.join(sim_dir, f)):
                msg.PrintMsg('\nERROR: Sniper output file does not exist:\n' \
                             '    ' + os.path.join(sim_dir, f))
                ret = -1

    # Define a set of strings which can be ignored as errors if they occur in
    # the output. These are special cases which must be added by hand when a
    # new application is found that contains a string which might be detected
    # as an erorr, but is a normal component of the output from running the
    # application.

    # Errors generated by Sniper itself which are acceptable.
    #
    ign_str = ['void Sift::Writer::Sync(): Assertion',
               'Sift::Reader::decodeInstruction']
    pin_app_term = 'Pin app terminated abnormally'
    ign_str += [pin_app_term]

    # Strings generated by SPEC CPU2006 benchmarks: dealII-ref-1
    #
    ign_str += ['Estimated error=']

    # Strings generated by MILC
    #
    ign_str += ['error_per_site', 'No O(a^2) errors', 'error_for_propagator']

    # Print the output from running Sniper and look for errors in the output.
    #
    error = False
    for pinball_path in pb_list:
        # Get just the pinball name & print it
        #
        fn = os.path.split(pinball_path)[1]

        sniper_outputfile = pinball_path + config.sniper_out_ext
        if os.path.isfile(sniper_outputfile):
            try:
                f_stdout = open(sniper_outputfile, 'r')
            except IOError:
                msg.PrintMsg(
                    'ERROR: Can\'t open Sniper output file to look for errors.\n'
                    '   ' + sniper_outputfile)
                ret = -1

            # Print the output file and look for errors.
            #
            if not options.list:
                if 'whole_program' in pinball_path:
                    msg.PrintMsg('\nSniper output for: ' + pinball_path)
                else:
                    msg.PrintMsg('\nSniper output for: ' + fn)
                for line in f_stdout.readlines():
                    msg.PrintMsgNoCR(line)
                    if not options.ignore_sniper_error and \
                        ('ERROR' in line or \
                         'error' in line or \
                         'Traceback' in line):

                        # Check to see if we can accept this line because
                        # it contains one of the strings we can ignore.
                        #
                        ok_ignore = False
                        for st in ign_str:
                            if st in line:
                                ok_ignore = True
                                break
                        if not ok_ignore:
                            # It's truly an error
                            #
                            error = True

                        # Need to print a msg indicating this error is OK to ignore.
                        #
                        if pin_app_term in line:
                            msg.PrintMsg(
                                'The \'Pin app terminated\' msg can be ignored. '
                                'It is not an error, just a warning.')
                if error:
                    msg.PrintMsg('\nERROR: Sniper failed with an error.')
                    ret = -1
                    error = False  # Reset in order to look for error in next pb
    return ret


def CalcPredError(wp_pb_dir, sim_replay_cmd, options):

    import locale
    sys.path.append(os.path.join(options.sniper_root, 'tools'))
    import sniper_lib

    def get_cpi(dir):
        try:
            r = sniper_lib.get_results(0, dir)
        except ValueError:
            msg.PrintMsg(
                '\nERROR: Can\'t get sniper results for:\n       ' + dir)
            return 0.0
        stats = r['results']
        if stats['ncores'] != 1:
            msg.PrintMsgPlus(
                'Warning: Sniper only supports single-threaded SimPoints')

        # This code works with both Sniper/SniperLite
        #
        instrs = stats['core.instructions']
        fs_to_cycles = stats['fs_to_cycles_cores']
        fs = stats['barrier.global_time']
        for i, d in enumerate(instrs):
            cpi = (float(fs[0]) * fs_to_cycles[i]) / instrs[i]

        if sum(instrs) <= 0:
            msg.PrintMsgPlus(
                '\nERROR: No cycles found in Sniper output for pinball:\n      '
                + dir)
            cpi = 0.0

        return cpi

    result = 0
    for wpdir in glob.glob(os.path.join(config.sniper_result_dir, wp_pb_dir,
                                        util.GetLogFile() + '*')):
        percent_cpi = []
        cpi = 0.0
        zero_cpi = False
        for ppdir in glob.glob(os.path.join(config.sniper_result_dir,
                                            os.path.basename(wpdir) + '.pp',
                                            os.path.basename(wpdir) + '*')):
            percent = float(
                re.search(r'warmup.*prolog.*region.*epilog.*_(\d-\d+)\.',
                          ppdir).group(1).replace('-', '.'))
            cpi = get_cpi(ppdir)
            if cpi == 0.0:
                # Skip this pinball if CPI is 0.0 (which probably means an error).
                #
                zero_cpi = True
                continue
            percent_cpi.append((percent, cpi))
        if zero_cpi:
            continue
        abs_diff_from_1 = abs(sum(map(lambda x: x[0], percent_cpi)) - 1.0)
        if abs_diff_from_1 > 0.00005:
            msg.PrintMsgPlus(
                'Warning: Weights for all regions don\'t sum up to 1.0, [abs(sum - 1.0) = %6.5f]'
                % abs_diff_from_1)

        # import pdb;  pdb.set_trace()
        msg.PrintMsg('\n%s' % os.path.split(wpdir)[1])
        predict_cpi = sum(map(lambda x: x[0] * x[1], percent_cpi))
        msg.PrintMsg('  Intermediate result, predicted CPI:           ' +
                     str(locale.format('%7.4f', predict_cpi, True)))
        if predict_cpi == 0.0:
            if options.mode == config.MPI_MT_MODE:
                msg.PrintMsgPlus('WARNING: Unable to get predicted CPI from region pinballs because of a problem with Sniper results:\n' \
                    '        ' + wpdir + '\n     Prediction error for this process will not be calcuated.')
                msg.PrintMsg('Since tracing mode is \'mpi_mt\', this may be OK.')
                continue
            else:
                # Indicate there was an error, skip this process and try the next one.
                #
                msg.PrintMsgPlus('ERROR: Unable to get predicted CPI from region pinballs because of a problem with Sniper results:\n' \
                    '        ' + wpdir + '\n     Prediction error for this process will not be calcuated.')
                result = -1
                continue
        measure_cpi = get_cpi(wpdir)
        msg.PrintMsg('  Intermediate result, measured CPI:            ' +
                     str(locale.format('%7.4f', measure_cpi, True)))
        if measure_cpi == 0.0:
            # Indicate there was an error, skip this process and try the next one.
            #
            msg.PrintMsg('\nERROR: Unable to get measured CPI from WP pinballs because of a problem with Sniper results:\n' \
                '        ' + wpdir + '\n     Prediction error will not be calcuated')
            result = -1
            continue
        msg.PrintMsg('\n%s' % os.path.split(wpdir)[1])
        msg.PrintMsg('  Predicted CPI:           ' +
                     str(locale.format('%7.4f', predict_cpi, True)))
        msg.PrintMsg('  Measured CPI:            ' +
                     str(locale.format('%7.4f', measure_cpi, True)))
        msg.PrintMsg('  Prediction error:        ' + str(locale.format(
            '%7.4f', 1 - (predict_cpi / measure_cpi), True)) + ' 1-(p/m)')
        msg.PrintMsg('  [Functional correlation: ' + str(locale.format(
            '%7.4f', predict_cpi / measure_cpi, True)) + ' (p/m)]')

    return result


class SniperPinPoints(pinpoints.PinPoints):

    sim_replay_cmd = 'run-sniper'

    ###################################################################################
    #
    # Run phases only in Sniper, not PinPlay
    #
    ###################################################################################

    def PrintHome(self, options):
        """
        Print the directories where the Pin kit and Sniper are located.

        @return No return value
        """

        super(SniperPinPoints,
              self).PrintHome(options)  # Call base class method PrintHome()
        if options.sniper_root:
            msg.PrintMsg('Sniper_root:               ' +
                         os.path.realpath(options.sniper_root))
        else:
            msg.PrintMsgPlus('WARNING: Sniper root not defined.\n')
        if options.sniper_options:
            msg.PrintMsg('Sniper options:            ' + options.sniper_options)
        if options.no_sniperlite or options.sniper_options:
            msg.PrintMsg('Sniper type:               Full Sniper simulator')
        else:
            msg.PrintMsg('Sniper type:               Sniperlite')

    def AddAdditionalOptions(self, parser):
        """Add additional phase options which Sniper needs, but PinPlay doesn't use."""

        sniper_root(parser)
        sniper_options(parser)
        ignore_sniper_error(parser)
        no_sniperlite(parser)

    def AddAdditionalPhaseOptions(self, parser, phase_group):
        """Add additional phase options which SDE needs, but PinPlay doesn't use."""

        region_sim(parser, phase_group)
        whole_sim(parser, phase_group)
        cmd_options.pred_error(parser, phase_group)

    def RunAdditionalPhases(self, wp_pb_dir, sim_replay_cmd, options):
        """Run the additional phases for SDE which are not run for PinPlay."""

        if not options.sniper_root or not os.path.exists(
            os.path.join(options.sniper_root, 'run-sniper')):
            msg.PrintMsgPlus(
                'ERROR: Please set SNIPER_ROOT or --sniper_root to a valid Sniper install.')
            util.CheckResult(-1, options, 'Checking for Sniper install location.'
                            )  # Force error with -1

        if options.region_sim or options.default_phases:
            # Print out Sniper results every warmup_length instructions.
            #
            for pp_dir in util.GetRegionPinballDir():
                phase_length = options.warmup_length / config.instr_cmpsim_phase
                if phase_length == 0:
                    phase_length = 1

                if not options.list:
                    msg.PrintMsgDate('Running Sniper on region pinballs %s' % \
                        config.PhaseStr(config.sniper_regions))
                util.PhaseBegin(options)
                result = RunSniper(pp_dir, sim_replay_cmd, phase_length, options)
                if not options.list:
                    msg.PrintMsgDate('Finished running Sniper on region pinballs %s' % \
                        config.PhaseStr(config.sniper_regions))
                util.CheckResult(result, options, 'Sniper on region pinballs %s' % \
                    config.PhaseStr(config.sniper_regions))

        if options.whole_sim or options.default_phases:
            # Set phase_length to print out Sniper results every slice_size instructions.
            #
            phase_length = options.slice_size / config.instr_cmpsim_phase
            if phase_length == 0:
                phase_length = 1

            if not options.list:
                msg.PrintMsgDate('Running Sniper on whole program pinballs %s' % \
                    config.PhaseStr(config.sniper_whole))
            util.PhaseBegin(options)
            result = RunSniper(wp_pb_dir, sim_replay_cmd, phase_length, options)
            if not options.list:
                msg.PrintMsgDate('Finished running Sniper on whole program pinballs %s' % \
                    config.PhaseStr(config.sniper_whole))
            util.CheckResult(result, options, 'Sniper on whole program pinballs %s' % \
                    config.PhaseStr(config.sniper_whole))

        if options.pred_error or options.default_phases:
            if options.pccount_regions:
                msg.PrintMsg(
                    '\n Prediction with PCregions is NIY')
                return 0
            if not options.list:
                msg.PrintMsgDate('Calculating prediction error %s' % \
                    config.PhaseStr(config.pred_error))
            util.PhaseBegin(options)
            result = CalcPredError(wp_pb_dir, self.sim_replay_cmd, options)
            if not options.list:
                msg.PrintMsgDate('Finished calculating prediction error %s' % \
                    config.PhaseStr(config.pred_error))
            util.CheckResult(result, options, 'Prediction error calculation %s' % \
                    config.PhaseStr(config.pred_error))

        # Assume nothing has gone wrong at this point.
        #
        return 0


def main():
    """ Process command line arguments and run the script """

    pp = SniperPinPoints()
    result = pp.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
