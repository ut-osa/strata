#! /usr/bin/env python

from __future__ import print_function

import argparse
from collections import defaultdict
import json
from math import ceil
import matplotlib
matplotlib.use('Agg')
from matplotlib import pyplot
import numpy as np
import os
import re
import subprocess
from subprocess import PIPE
import sys


trials = {
    'sr': {
      'sizes': ['1G', '2G', '4G'],
      'iosizes': ['4K', '16K', '64K'],
      'threads': ['1', '2', '3', '4']
    }
  }

# For a single IO amount, we have multiple throughput measurements.
# Reduce that to (average, std dev) for all points on the line,
def calc_y_vals(raw_y_vals):
  averages = []
  std_devs = []
  for y_arr in raw_y_vals:
    averages.append(abs(np.mean(y_arr)))
    std_devs.append(np.std(y_arr))

  return (averages, std_devs)

def do_multi_data_plot(x_vals, y_vals, y_errs, names_arr, outname):
  line_arr = ["b", "g", "r", "k"]

  # Find the max y value in the whole graph -- helps set graph dimensions.
  y_max = max([y_arr[-1] for y_arr in y_vals])
  x_max = max([x_arr[-1] for x_arr in x_vals])

  bar_width = 0.35

  first = True
  for n, x_arr, y_arr, c, name in zip(range(0, len(x_vals)), x_vals, y_vals, line_arr, names_arr):
    print(name)
    index = np.arange(len(x_arr))
    pyplot.bar(index + (bar_width * n), y_arr, bar_width, color=c, label=name)
    first = False

  pyplot.xticks(x_vals[0])
  pyplot.legend(loc=2)
  pyplot.xlabel("Thread Count")
  pyplot.ylabel("Total Throughput (MB/sec)")
  pyplot.savefig(outname, bbox_inches="tight")
  pyplot.close()


def run_iotest(trials, iotest_args):
  results = []
  res_regex = re.compile(r"Aggregated throughput: ([0-9\.]+) MB/sec")
  args = ["sudo", "./run.sh", "iotest"]
  args += [str(arg) for arg in iotest_args if arg is not None and arg != ""]

  for i in range(0, trials):
    proc = subprocess.Popen(args, stdout=PIPE, stderr=PIPE)
    proc_out, proc_err = proc.communicate()
    if proc.returncode != 0:
      print("Subprocess {} failed with code {}".format(
          " ".join(args), proc.returncode))
      print(proc_err.strip())
      print(proc_out.strip())
      return -1

    m = res_regex.search(proc_out, re.MULTILINE)
    if m is None or len(m.groups()) == 0:
      print("Error: iotest did not output data in the expected format")
      print(proc_out.strip())
      return -1

    throughput = m.group(1)
    try:
      results.append(float(throughput))
    except ValueError:
      print("Error: throughput '{}' is not a number".format(throughput))
      return -1

  return results


def do_trials(args):
  # Generate all the tests we want to do.
  tests = []
  results = defaultdict(dict)
  names = {}

  for mode in trials:
    if not args.concurrent:
      trials[mode]['threads'] = ['1']

    for size in trials[mode]['sizes']:
      for iosize in trials[mode]['iosizes']:
        test_name = "iotest {} with IO size of {}".format(mode, iosize)
        results[test_name][size] = []

        for threads in trials[mode]['threads']:
          test = [mode, size, iosize, threads]
          res = run_iotest(args.trials, test)
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

    do_multi_data_plot(x_mat, y_mat, err_mat, names, outname)


def main():
  # Parse arguments, have a nice help message, and generally pretend that I know
  # how to write good software.
  parser = argparse.ArgumentParser(
      description="Run a set of benchmarks and aggregate the results.")


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
