#! /usr/bin/env python
# This is a script for running iobench and graphing the results from it.
# 'run' will create/append to json files that contain the trial results from
# running iobench. This data may not be formatted for optimal speed, but I
# found it to be easier to read by humans (if you want to look at the
# performance results more textually). 'graph' then takes json files and creates
# graphs using matplotlib. The graphs under strata/docs/img were created using
# this script.
from __future__ import print_function

import argparse
import json
from collections import defaultdict
from math import ceil

import matplotlib
matplotlib.use('Agg')
from matplotlib import pyplot
import numpy as np
import os
import re
from time import sleep
import subprocess
from subprocess import PIPE
import sys

import progressbar


trials = {
    'sw': {
      'sizes': ['2G', '4G'],
      'iosizes': ['16K'],
      'threads': []
    }
    'sr': {
      'sizes': ['1G'],
      'iosizes': ['16K'],
      'threads': []
    }
  }

# For a single IO amount, we have multiple throughput measurements.
# Reduce that to (average, std dev) for all points on the line,
def calc_y_vals(raw_y_vals):
  averages = []
  std_devs = []
  for y_arr in [y for y in raw_y_vals if y > 0]:
    averages.append(np.mean(y_arr))
    std_devs.append(np.std(y_arr))

  return (averages, std_devs)

def do_multi_data_plot(x_vals, y_vals, y_errs, names_arr, outname, title):
  line_arr = ["b", "g", "r", "k"]

  # Find the max y value in the whole graph -- helps set graph dimensions.
  y_max = max([y_arr[-1] for y_arr in y_vals])
  x_max = max([x_arr[-1] for x_arr in x_vals])

  bar_width = 0.5 / float(len(x_vals))

  first = True
  for n, x_arr, y_arr, c, name in zip(range(0, len(x_vals)), x_vals, y_vals, line_arr, names_arr):
    print(name)
    index = np.arange(len(x_arr))
    pyplot.bar(index + (bar_width * n), y_arr, bar_width, color=c, label=name,
               align='center')
    first = False

  pyplot.title(title)
  pyplot.xticks(np.arange(len(x_vals[0])), sorted(x_vals[0]))
  #pyplot.legend(loc=2)
  pyplot.xlabel("Thread Count")
  pyplot.ylabel("Total Throughput (MB/sec)")
  pyplot.savefig(outname, bbox_inches="tight")
  pyplot.close()


def run_iobench(trials, iobench_args):
  results = []
  res_regex = re.compile(r"Aggregated throughput: ([0-9\.]+) MB/sec")
  args = ["sudo", "./run.sh", "iobench"]
  args += [str(arg) for arg in iobench_args if arg is not None and arg != ""]

  bar = progressbar.ProgressBar()

  for i in bar(range(0, trials)):
    proc = subprocess.Popen(args, stdout=PIPE, stderr=PIPE)
    try:
      proc_out, proc_err = proc.communicate()
      if proc.returncode != 0:
        print("Subprocess {} failed with code {}".format(
            " ".join(args), proc.returncode))
        print(proc_err.strip())
        print(proc_out.strip())
        return results

      m = res_regex.search(proc_out, re.MULTILINE)
      if m is None or len(m.groups()) == 0:
        print("Error: iobench did not output data in the expected format")
        print(proc_out.strip())
        return results

      throughput = m.group(1)
      try:
        results.append(float(throughput))
      except ValueError:
        print("Error: throughput '{}' is not a number".format(throughput))
        return results
    except KeyboardInterrupt:
      if proc.returncode is None:
        proc.terminate()
        proc.wait()
      print("Aborting tests early...")
      return results

  return results


def do_trials(args):
  # Generate all the tests we want to do.
  tests = []
  results = defaultdict(dict)
  names = {}

  def get_default_dict(args):
    return defaultdict(dict, args)


  if os.path.exists(args.outfile):
    with open(args.outfile, 'r') as f:
      results = json.load(f, object_pairs_hook=get_default_dict)

  for mode in sorted(trials.keys(), reverse=True):
    if not args.concurrent:
      trials[mode]['threads'] = ['1']
    else:
      #trials[mode]['threads'] = ['2', '3', '4']
      trials[mode]['threads'] = ['4']

    for iosize in trials[mode]['iosizes']:
      test_name = "iobench {} with IO size of {}".format(mode, iosize)
      for size in trials[mode]['sizes']:
        if size not in results[test_name]:
          results[test_name][size] = []

        for threads in trials[mode]['threads']:
          test = [mode, size, iosize, threads]
          print("Running iobench with args: {}".format(test))
          res = run_iobench(args.trials, test)
          if len(results[test_name][size]) > 0:
            added = False
            for num, points in results[test_name][size]:
              if num == int(threads):
                points.extend(res)
                added = True
            if not added:
              results[test_name][size] += [(int(threads), res)]
          else:
            results[test_name][size] += [(int(threads), res)]

  with open(args.outfile, "w") as f:
    json.dump(results, f, sort_keys=False, indent=4, separators=(",", ": "))


def do_graph(args):
  data_sets = defaultdict(dict)
  size_regex = re.compile(r'([0-9])+([a-zA-Z])')

  for f in args.files:
    with open(f, "r") as fp:
      data = json.load(fp)
      for key in data:
        data_sets[key].update(data[key])

  for test_name in data_sets:
    x_mat = []
    y_mat = []
    err_mat = []
    outname = "_".join(test_name.split()) + ".png"
    data_set = data_sets[test_name]
    names = []

    for size in sorted(data_set.keys()):
      names.append("{} (IO/thread)".format(size))
      points = data_set[size]
      line_x = []
      line_y = []
      for x, y_arr in points:
        line_x.append(int(x))
        line_y.append(y_arr)
      y_points, y_err = calc_y_vals(line_y)

      x_mat.append(line_x)
      y_mat.append(y_points)
      err_mat.append(y_err)

    do_multi_data_plot(x_mat, y_mat, err_mat, names, outname, test_name)


def main():
  # Parse arguments, have a nice help message, etc.
  parser = argparse.ArgumentParser(
      description="Run a set of benchmarks and aggregate the results.")

  # Sub-commands are nifty.
  subparser = parser.add_subparsers(help="sub-command help")

  parser_run = subparser.add_parser("run", help="run help")
  parser_run.add_argument("trials", type=int,
                          help="Number of trials to run per test")
  parser_run.add_argument("outfile",
                          help="Destination file for command output.")
  parser_run.add_argument("-c, --concurrent", action="store_true",
                          dest="concurrent",
                          help=("Also run multithreaded tests. Only use this "
                                "option if libfs compiled with -DCONCURRENT."))
  parser_run.set_defaults(func=do_trials)

  parser_graph = subparser.add_parser("graph", help="graph help")
  parser_graph.add_argument("files", metavar="json...", nargs="+",
                            help="Which files to pull data to graph.")
  parser_graph.set_defaults(func=do_graph)

  args = parser.parse_args()
  args.func(args)


if __name__ == "__main__":
  main()
