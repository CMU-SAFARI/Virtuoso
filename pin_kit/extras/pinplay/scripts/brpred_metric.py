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
# $Id: brpred_metric.py,v 1.28 2015/08/15 20:02:00 tmstall Exp tmstall $

import sys
import os
import optparse
import glob

# Local modules
#
import cmd_options
import config
import brpred_kit
import msg
import phases
import util


class BrPredMetric(object):
    """
    Top level script to calculate the predicted metric of interest, actual
    metric of interest and prediction error from simulator data files.
    """

    # Objects used in this class.
    #
    kit_obj = None
    phases = phases.Phases()

    def GetKit(self):
        """ Get the Branch Predictor simulation kit. """

        kit_obj = brpred_kit.BrPredKit()
        return kit_obj

    def ParseCommandLine(self):
        """Process command line arguments and do error checking."""

        version = '$Revision: 1.28 $'
        version = version.replace(' ', '')
        ver = version.replace(' $', '')
        us = '%prog sim_data_dir | options \nVersion: ' + ver
        desc = 'When run with only the argument \'sim_data_dir\', the script calculates the predicted '\
               'metric of interest from the simulator data file contained in this directory. '\
               'If any options are given, then only the actions defined by the options will be executed. '\
               '                                                            '\
               '                                                            '\
               'NOTE: Must specify both options \'--actual_metric\' and \'--predicted_metric\' '\
               'when using the option \'--pred_error\'.'

        # Command line options to control the tool's behavior.
        #
        util.CheckNonPrintChar(sys.argv)
        parser = optparse.OptionParser(usage=us, description=desc, version=ver)
        cmd_options.debug(parser)
        cmd_options.pred_error(parser, '')
        cmd_options.actual_metric(parser)
        cmd_options.pinplayhome(parser, '')
        cmd_options.predicted_metric(parser)
        cmd_options.verbose(parser)

        # Parse the command line options.
        #
        (options, args) = parser.parse_args()

        # Added method cbsp() to 'options' to check if running CBSP.
        #
        util.AddMethodcbsp(options)

        # Error if no arguments or options on command line.
        #
        if len(sys.argv) == 1:
            msg.PrintAndExit(
                'Give either the name of a directory containing\n'
                'simulator data files or one, or more, options on the command line.\n'
                'See \'-h\' for more info.')

        # Make sure all required options are given if user wants prediction
        # error.
        #
        if options.pred_error:
            if options.predicted_metric == '' or options.actual_metric == '':
                msg.PrintAndExit('Must give both \'--predicted_metric\' and '
                                 '\'--actual_metric\' with \'--pred_error\'.')

        predicted_dir = ''
        actual_dir = ''
        if len(args) == 1:
            # If there are only 2 arguments, assume just calculating predicted
            # metric.  Check to see if a valid directory name was given on the
            # command line.  
            #
            dir_found = False
            for dir_name in args:
                if os.path.isdir(os.path.realpath(dir_name)):
                    dir_found = True
                    break
            if not dir_found:
                msg.PrintAndExit(
                    '\'' + sys.argv[1] + '\' is not a valid directory.\n'
                    'Give either the name of a directory containing simulator data files \n'
                    'or one, or more, options on the command line.\n'
                    'See \'-h\' for more info.')
            predicted_dir = os.path.realpath(dir_name)
        else:
            # Check directories given as arguments to options to make sure they exist.
            #
            if options.predicted_metric:
                predicted_dir = os.path.realpath(options.predicted_metric)
                if not os.path.isdir(predicted_dir):
                    msg.PrintAndExit('Predicted metric directory does not exist: ' + \
                        options.predicted_metric)
            if options.actual_metric:
                actual_dir = os.path.realpath(options.actual_metric)
                if not os.path.isdir(actual_dir):
                    msg.PrintAndExit('Actual metric directory does not exist: ' + \
                        options.actual_metric)

        # Set the global variables in config object from the options.  Thus, if the
        # user sets option 'pinplayhome' on the command line, it will be used when
        # determining which kit to use.
        #
        # import pdb;  pdb.set_trace()
        gv = config.GlobalVar()
        gv.SetGlobalVars(options)
        self.kit_obj = self.GetKit()

        return (predicted_dir, actual_dir, options)

    def Run(self):
        """Execute the desired behavior."""

        # import pdb;  pdb.set_trace()
        (predicted_dir, actual_dir, options) = self.ParseCommandLine()

        # Default values.
        #
        predicted_metric = -1
        actual_metric = -1

        # Get the predicted metric of interest for a set of simulator data file.
        #
        # import pdb;  pdb.set_trace()
        if predicted_dir:
            predicted_metric = self.phases.GetPredictMetric(
                predicted_dir, self.kit_obj, options)
            if predicted_metric == -1:
                msg.PrintAndExit(
                    'Problem occurred calculating the predicted metric')
            msg.PrintMsg('Predicted metric:       %10.4f' % predicted_metric)

        # Get the actual (measured) metric of interest for a data file.
        #
        # import pdb;  pdb.set_trace()
        multiple_files = False
        if actual_dir:
            sim_files = glob.glob(
                os.path.join(actual_dir, '*' + self.kit_obj.file_ext + '*'))
            if len(sim_files) > 1:
                multiple_files = True

            # If there are more than one data files in the directory, then
            # print out the file name so the metrics are labeled appropriately
            #
            for name in sim_files:
                actual_metric = self.kit_obj.GetLastMetric(name, 0, options)
                if actual_metric == -1:
                    msg.PrintAndExit(
                        'Problem occurred getting measured metric for: ' +
                        actual_dir)
                # import pdb;  pdb.set_trace()
                if not multiple_files and not options.verbose:
                    msg.PrintMsg(
                        'Actual metric:          %10.4f' % actual_metric)
                else:
                    msg.PrintMsg('Actual metric:              %s\t%s' % str(
                        locale.format('%7.4f', actual_metric), True),
                                 os.path.basename(name))
                    msg.PrintMsg('Actual metric:          %10.4f\t%s' % (actual_metric, \
                        os.path.basename(name)))

        # If both required metrics were already calculated, and the user wants
        # the prediction error, then print it.  NOTE: If there were
        # multiple data files in 'actual_dir', then a prediction error
        # can not be calculated.
        #
        if options.predicted_metric != '' and options.actual_metric != '' and options.pred_error:
            if not multiple_files:
                msg.PrintMsg('Prediction error:          ' + str(locale.format(
                    '%7.4f', 1 -
                    (predict_metric / actual_metric), True)) + ' 1-(p/m)')
                msg.PrintMsg('[Functional correlation:   ' + str(locale.format(
                    '%7.4f', predict_metric / actual_metric, True)) + ' (p/m)]')
            else:
                msg.PrintMsg(
                    'ERROR: Unable to calculate prediction error with multiple sets of data files.')

        # Any errors have caused the script to already exit.
        #
        return 0


def main():
    """ Process command line arguments and run the script """

    bp = BrPredMetric()
    result = bp.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    if (result > 0):
        sys.exit(result)
