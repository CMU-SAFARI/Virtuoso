#!/usr/bin/env python
#
# BEGIN_LEGAL
# BSD License
#
# Copyright (c)2017 Intel Corporation. All rights reserved.
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
# Script to run Simpoint then generate region CSV files.
#
# $Id: simpoint.py,v 1.33 2015/10/28 22:28:06 tmstall Exp tmstall $

import os
import sys
import shutil
import optparse

import cmd_options
import config
import msg
import util


class SimPoint(object):

    platform = util.Platform()
    if platform == config.WIN_NATIVE or platform == config.WIN_CYGWIN:
        simpoint_bin = 'simpoint.exe'
    else:
        simpoint_bin = 'simpoint'
    csv_bin = 'regions.py'
    generic_bbv_name = 't.bb'
    generic_ldv_name = 't.ldv'
    proj_bbv_file = 'projected_t.bb'
    weight_ldv_file = 'weighted_t.ldv'
    freq_vect_file = 't.fv'

    def ParseCommandLine(self):
        """
        Get the options from the command line and check for errors.

        @return tuple with parsed options and unparsed args
        """

        # Define and get command line options.
        #
        version = '$Revision: 1.33 $'
        version = version.replace('$Revision: ', '')
        ver = version.replace(' $', '')
        us = '%prog --bbv_file FILE --data_dir DIR FILE --simpoint_file FILE [options] --pccount_regions --warmup_factor W'
        desc = 'Runs Simpoint and then generates the region CSV file.  ' \
               'Input to Simpoint can be just an BBV file or a combination of BBV/LDV files. \n\n' \
                'Required options: --bbv_file, --data_dir, --simpoint_file'

        util.CheckNonPrintChar(sys.argv)
        parser = optparse.OptionParser(
            usage=us,
            version=ver,
            description=desc,
            formatter=cmd_options.BlankLinesIndentedHelpFormatter())

        cmd_options.debug(parser)
        cmd_options.global_file(parser)
        cmd_options.list(parser, '')
        cmd_options.bbv_file(parser, '')
        cmd_options.data_dir(parser)
        cmd_options.simpoint_file(parser)
        cmd_options.ldv(parser, '')
        cmd_options.combine(parser, '')
        cmd_options.cutoff(parser, '')
        cmd_options.focus_thread(parser, '')
        cmd_options.maxk(parser, '')
        cmd_options.global_regions(parser, '')
        cmd_options.warmup_factor(parser, '')
        cmd_options.pccount_regions(parser, '')
        cmd_options.num_cores(parser, '')
        cmd_options.simpoint_options(parser, '')

        (options, args) = parser.parse_args()

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        # Must have option '--ldv', even if using option '--combine', in order to
        # process BBV/LDV both.  Let user know if '--combine' used w/o '--ldv'.
        #
        if not options.ldv and options.combine != -1.0:
            msg.PrintMsgPlus('WARNING: Option \'--combine\' detected without \'--ldv\'.  Only using BBV for ' \
                'Simpoint.  \n              Must explicitly specify \'--ldv\' in order to use both BBV/LDV.\n')
        if options.ldv:
            msg.PrintMsgPlus('Using both BBV/LDV files when running Simpoint\n')

        # If option combine is not set, then set it to the default value.
        # Check to make sure combine an acceptable value.  
        #
        util.SetCombineDefault(options)
        util.CheckCombine(options)

        # Read in an optional configuration files and set global variables.
        #
        config_obj = config.ConfigClass()
        config_obj.GetCfgGlobals(options,
                                 False)  # Don't need to require 4 variables

        # Error check input to make sure all required options are on the command line.
        #
        if options.bbv_file == '':
            msg.PrintAndExit(
                'Basic block vector file must be defined with option: --bbv_file FILE')
        if options.warmup_factor != 0 and  not options.pccount_regions:
            msg.PrintAndExit(
                '--warmup_factor can only be specified with --pccount_regions')
        if options.data_dir == '':
            msg.PrintAndExit(
                'Simpoint data directory must be defined with option: --data_dir DIR')
        if options.simpoint_file == '':
            msg.PrintAndExit(
                'Simpoint output must be defined with option: --simpoint_file FILE')

        # The data_dir should exist and contain the BBV file.
        #
        if not os.path.isdir(options.data_dir):
            msg.PrintAndExit(
                'Data directory does not exist: ' + options.data_dir)
        if not os.path.isfile(os.path.join(options.data_dir, options.bbv_file)):
            msg.PrintAndExit(
                'Basic block vector file does not exist: ' + options.bbv_file)

        # Do some 'special' things on native Windows.
        #
        util.WindowsNativeCheck(options)

        return (options, args)

    def NormProjectBBV(self, options):
        """
        Normalize and project the basic block vector file instead of doing this in Simpoint.

        This is required so we can combine BBV and LDV files as frequency vector file given
        to Simpoint.

        @param options Options given on cmd line

        @return result of command to generate projected file
        """

        # Format the command and run it.
        #
        # import pdb;  pdb.set_trace()

        # Use options for the Python script to generate the CSV file.
        #
        output_file = 'normalize_project.out'
        try:
            fp_error = open(output_file, 'w')
        except IOError:
            msg.PrintMsg('ERROR: Failed to open normalize/project error file:\n'
                         '   ' + output_file)
            return -1
        try:
            fp_out = open(self.proj_bbv_file, 'w')
        except IOError:
            msg.PrintMsg(
                'ERROR: Failed to open normalize/project output file:\n'
                '   ' + self.proj_bbv_file)
            fp_error.close()
            return -1

        cmd = self.csv_bin
        cmd += ' --project_bbv'
        cmd += ' --bbv_file'
        cmd += ' ' + self.generic_bbv_name
        cmd += ' --dimensions 16'

        # msg.PrintMsg('')
        # print 'cmd: ' + cmd
        # import pdb;  pdb.set_trace()
        result = util.RunCmd(cmd, options, '', concurrent=False, f_stdout=fp_out, \
                             f_stderr=fp_error)
        msg.PrintMsg('   Output file: %s' % (self.proj_bbv_file))
        msg.PrintMsg('   Stderr file: %s\n' % (output_file))
        fp_out.close()
        fp_error.close()

        return result

    def NormWeightLDV(self, options):
        """
        Normalize and apply weights to LRU stack distance vector files.

        @param options Options given on cmd line

        @return result of command to generate file
        """

        # Ensure there's an LDV file to process.
        #
        if not os.path.isfile(self.generic_ldv_name):
            msg.PrintMsg('ERROR: Can\'t open LDV vector file.\n '
                         '   ' + self.generic_ldv_name)
            return -1

        # Use options for the Python script to process the LDV file.
        #
        # import pdb;  pdb.set_trace()
        output_file = 'normalize_weight.out'
        try:
            fp_error = open(output_file, 'w')
        except IOError:
            msg.PrintMsg('ERROR: Failed to open normalize weights error file:\n'
                         '   ' + output_file)
            return -1
        try:
            fp_out = open(self.weight_ldv_file, 'w')
        except IOError:
            msg.PrintMsg(
                'ERROR: Failed to open normalize weights output file:\n'
                '   ' + self.weight_ldv_file)
            fp_error.close()
            return -1

        cmd = self.csv_bin
        cmd += ' --weight_ldv '
        cmd += ' --ldv_file ' + self.generic_ldv_name
        cmd += ' --dimensions 16'

        # msg.PrintMsg('')
        # print 'cmd: ' + cmd
        # import pdb;  pdb.set_trace()
        result = util.RunCmd(cmd, options, '', concurrent=False, f_stdout=fp_out, \
                             f_stderr=fp_error)
        msg.PrintMsg('   Output file: %s' % (self.weight_ldv_file))
        msg.PrintMsg('   Stderr file: %s\n' % (output_file))
        fp_out.close()
        fp_error.close()

        return result

    def CombineFreqVectFiles(self, options):
        """
        Combine the BBV and LDV files, applying a scaling factor to allow
        different contributions from each file.

        @param options Options given on cmd line

        @return result of command to generate file
        """

        # Format the command and run it.
        #
        # import pdb;  pdb.set_trace()

        # Use options for the Python script to generate the CSV file.
        #
        output_file = 'scaled_combined.out'
        try:
            fp_error = open(output_file, 'w')
        except IOError:
            msg.PrintMsg('ERROR: Failed to open combined scale error file:\n'
                         '   ' + output_file)
            return -1
        try:
            fp_out = open(self.freq_vect_file, 'w')
        except IOError:
            msg.PrintMsg('ERROR: Failed to open combined scale output file:\n'
                         '   ' + self.freq_vect_file)
            fp_error.close()
            return -1

        cmd = self.csv_bin
        cmd += ' --combine ' + str(options.combine)
        string = 'Combining BBV and LDV files with scaling factors: BBV: %.3f, LDV: %.3f\n' % \
            (options.combine, 1 - options.combine)
        msg.PrintMsgPlus(string)
        fp_error.write(string)
        cmd += ' --normal_bbv ' + self.proj_bbv_file
        cmd += ' --normal_ldv ' + self.weight_ldv_file
        result = util.RunCmd(cmd, options, '', concurrent=False, f_stdout=fp_out, \
                             f_stderr=fp_error)
        msg.PrintMsg('   Output file: %s' % (self.freq_vect_file))
        msg.PrintMsg('   Stderr file: %s\n' % (output_file))
        fp_out.close()
        fp_error.close()

        return result

    def RunSimpoint(self, options):
        """
        Format and execute the command to run Simpoint.

        @param options Options given on cmd line

        @return result of running Simpoint
        """

        import subprocess

        # Output file for simpoint
        #
        output_file = 'simpoint.out'
        try:
            fp_out = open(output_file, 'w')
        except IOError:
            msg.PrintMsg('ERROR: Failed to open simpoint error file:\n'
                         '   ' + output_file)
            return -1

        # Format the Simpoint command and run it.
        #
        # import pdb;  pdb.set_trace()
        cmd = self.simpoint_bin
        if options.ldv:
            cmd += ' -fixedLength on -loadVectorsTxtFmt ' + self.freq_vect_file
        else:
            cmd += ' -loadFVFile ' + self.generic_bbv_name

        # Add either the default options used to configure Simpoints or the
        # Simpoint options from the user.
        #
        if options.simpoint_options == '':
            cmd += ' -coveragePct ' + str(options.cutoff)
            cmd += ' -maxK ' + str(options.maxk)
        else:
            cmd += ' ' + options.simpoint_options

        cmd += ' -saveSimpoints ./t.simpoints -saveSimpointWeights ./t.weights -saveLabels t.labels'
        result = util.RunCmd(cmd, options, '',
                             concurrent=False,
                             f_stdout=fp_out,
                             f_stderr=subprocess.STDOUT)
        msg.PrintMsg('   Output file: %s' % (output_file))
        msg.PrintMsg('   Stderr file: %s' % (output_file))
        fp_out.close()

        return result

    def GenRegionCSVFile(self, options):
        """
        Format and execute the command to generate the regions CSV file.

        @param options Options given on cmd line

        @return result of command to generating CSV file
        """
        # Setup some stuff for generating regions CSV files.
        #
        # import pdb;  pdb.set_trace()
        cutoff_suffix = ''
        if options.cutoff < 1.0:
            cutoff_suffix = '.lpt' + str(options.cutoff)
        pos = options.data_dir.find('.Data')

        # Output and error files
        #
        if options.global_regions:
          regions_csv_file = options.data_dir[:pos] + '.global.pinpoints.csv'
        else:
          regions_csv_file = options.data_dir[:pos] + '.pinpoints.csv'
        try:
            fp_out = open(regions_csv_file, 'w')
        except IOError:
            msg.PrintMsg('ERROR: Failed to open CSV output file:\n'
                         '   ' + regions_csv_file)
            return -1

        output_file = 'create_region_file.out'
        try:
            fp_error = open(output_file, 'w')
        except IOError:
            msg.PrintMsg('ERROR: Failed to open CSV error file:\n'
                         '   ' + output_file)
            fp_out.close()
            return -1

        # Format the command to generate the region CSV file and run it.
        #
        # import pdb;  pdb.set_trace()
        if options.focus_thread < 0:
            tid = 0
        else:
            tid = options.focus_thread

        # use_orig = True  # Use Chuck's original Perl script
        use_orig = False  # Use regions.py script
        if use_orig:
            # Use Chuck's Perl script to generate the CSV file.
            #
            cmd = 'create_region_file.pl'
            cmd += ' ' + self.generic_bbv_name
            cmd += ' -seq_region_ids -tid ' + str(tid)
            cmd += ' -region_file t.simpoints' + cutoff_suffix
            cmd += ' -weight_file t.weights' + cutoff_suffix
        else:
            # Use the new Python script to generate the CSV file.
            #
            if options.pccount_regions:
                cmd = 'pcregions.py'
                cmd += ' --label_file t.labels'
                if options.warmup_factor != '':
                    cmd += ' --warmup_factor '+ str(options.warmup_factor)
                cmd += ' --tid ' + str(tid)
            else:
                cmd = self.csv_bin
                cmd += ' -f ' + str(tid)
                cmd += ' --csv_region '
            cmd += ' --bbv_file ' + self.generic_bbv_name + cutoff_suffix
            cmd += ' --region_file t.simpoints' + cutoff_suffix
            cmd += ' --weight_file t.weights' + cutoff_suffix
        msg.PrintMsg('')
        msg.PrintMsg('cmd='+cmd)
        result = util.RunCmd(cmd, options, '', concurrent=False, print_time=False, f_stdout=fp_out, \
                             f_stderr=fp_error)
        msg.PrintMsg(
            '   NOTE: For this script, problems can be in either the output or stderr files.  Check them both!')
        msg.PrintMsg('      Output file: %s' % (regions_csv_file))
        msg.PrintMsg('      Stderr file: %s\n' % (output_file))
        fp_out.close()
        fp_error.close()

        return result, regions_csv_file

    def Run(self):
        """Run the scripts required to run simpoint and generate a region CSV file with the results."""

        msg.PrintMsg('')

        # Get the command line options
        #
        (options, args) = self.ParseCommandLine()

        # Make sure required utilities exist and are executable.
        #
        # import pdb;  pdb.set_trace()
        if util.Which(self.simpoint_bin) == None:
            msg.PrintAndExit('simpoint binary not in path.\n'
                             'Add directory where it exists to your path.')
        if util.Which(self.csv_bin) == None:
            msg.PrintAndExit(
                'script to generate the region CSV file not in path.\n'
                'Add directory where it exists to your path.')

        # Go to the data directory. Both utilities should be run in this directory.
        #
        os.chdir(options.data_dir)

        # Always copy the specific BBV to the generic name used by simpoint.
        # If LDV file exists, then copy it to generic name.
        #
        if os.path.isfile(self.generic_bbv_name):
            os.remove(self.generic_bbv_name)
        shutil.copy(options.bbv_file, self.generic_bbv_name)
        ldv_file = options.bbv_file.replace('.bb', '.ldv')
        if os.path.isfile(ldv_file):
            if os.path.isfile(self.generic_ldv_name):
                os.remove(self.generic_ldv_name)
            shutil.copy(options.bbv_file.replace('.bb', '.ldv'),
                        self.generic_ldv_name)

        # Get the instruction count and slice_size from the BBV file.
        #
        try:
            f = open(self.generic_bbv_name)
        except IOError:
            msg.PrintAndExit(
                'problem opening BBV file: ' + self.generic_bbv_name)
        instr_count = 0
        slice_size = 0
        for line in f.readlines():
            if line.startswith('Dynamic '):
                tmp = line.split()
                count = tmp[len(tmp) - 1]
                if count > instr_count:
                    instr_count = int(count)
            if line.startswith('SliceSize:'):
                tmp = line.split()
                size = tmp[len(tmp) - 1]
                if size > slice_size:
                    slice_size = int(size)

        # Check to make sure instruction count > slice_size.
        #
        if slice_size > instr_count:
            import locale
            locale.setlocale(locale.LC_ALL, "")
            msg.PrintAndExit('Slice size is greater than the number of instructions.  Reduce parameter \'slice_size\'.' + \
                '\nInstruction count: ' + locale.format('%14d', int(instr_count), True) + \
                '\nSlice size:        ' + locale.format('%14d', int(slice_size), True))

        if options.ldv:
            # Run to generate regions CSV file using both BBV and LDV files.
            #
            result = self.NormProjectBBV(options)
            util.CheckResult(
                result, options,
                'normalizing and projecting BBV with: ' + self.csv_bin)
            result = self.NormWeightLDV(options)
            util.CheckResult(
                result, options,
                'normalizing and applying weights to LDV with: ' + self.csv_bin)
            result = self.CombineFreqVectFiles(options)
            util.CheckResult(
                result, options,
                'scaling and combining BBV and LDV with: ' + self.csv_bin)
            result = self.RunSimpoint(options)
            util.CheckResult(
                result, options,
                'generating clusters (regions) with: ' + self.simpoint_bin)
            result, regions_csv_file = self.GenRegionCSVFile(options)
            util.CheckResult(result, options,
                             'creating regions CSV file with: ' + self.csv_bin)
            msg.PrintMsg('\nRegions CSV file: ' +
                         os.path.join(options.data_dir, regions_csv_file))
        else:
            # Run scripts to generate regions CSV file using the new method with just the BBV file.
            #
            result = self.NormProjectBBV(options)
            util.CheckResult(
                result, options,
                'normalizing and projecting BBV with: ' + self.csv_bin)
            result = self.RunSimpoint(options)
            util.CheckResult(
                result, options,
                'generating clusters (regions) with: ' + self.simpoint_bin)
            result, regions_csv_file = self.GenRegionCSVFile(options)
            util.CheckResult(result, options,
                             'creating regions CSF file with: ' + self.csv_bin)
            msg.PrintMsg('\nRegions CSV file: ' +
                         os.path.join(options.data_dir, regions_csv_file))

        return result


def main():
    """ Process command line arguments and run the script """

    f = SimPoint()
    result = f.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
