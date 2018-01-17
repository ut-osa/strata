#! /usr/bin/env python

from __future__ import print_function

import argparse
import json
from math import ceil
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
      'threads': ['1', '2', '4']
    }
  }

# Polynomial Regression
def polyreg(x, y, f):
	# fit values, and mean
	yhat = f(x)
	ybar = np.sum(y)/len(y)
	ssreg = np.sum((yhat - ybar)**2)
	sstot = np.sum((y - ybar)**2)
	return ssreg / sstot


def parse_from_json(json_file):
  """
      Parse the graph data from the JSON files, which is in an array of x,y
      tuples, into a tuple of x,y arrays.
  """
  x_arr = []
  y_arr = []
  with open(json_file, "r") as f:
    data = json.load(f)
    for x, y in data:
      x_arr += [x]
      y_arr += [y]

  return (np.array(x_arr), np.array(y_arr))


def do_single_data_plot(data, lang, outname, degree):
  # To plot the idealized n^3 curve.
  t = np.arange(1, 1024, 2)
  f = lambda x: (data[1][-1]/(1024.0**degree))*(x**degree)

  curve_fit = polyreg(*data, f=f)

  lines = pyplot.plot(data[0], data[1], "b-")
  lines += pyplot.plot(t, f(t), "g--")
  pyplot.legend(lines,
                ["{}".format(lang), r'O($n^{%s}$)' % degree],
                loc=2)
  pyplot.axis([0, 1024, 0, ceil(data[1][-1])])
  pyplot.ylabel("Average Execution Time (seconds)")
  pyplot.xlabel("Array Size (N, for matrix of size NxN)")
  pyplot.text(60, ceil(data[1][-1])/2, r'determination=${0:.2f}$'.format(curve_fit))
  pyplot.savefig(outname, bbox_inches="tight")
  pyplot.close()


def do_multi_data_plot(data_arr, lang_arr, outname):
  line_arr = ["b-", "g--", "r:", "k-."]

  y_max = max([data[1][-1] for data in data_arr])

  lines = []
  for data, linetype in zip(data_arr, line_arr):
    lines += pyplot.plot(data[0], data[1], linetype)

  pyplot.legend(lines, lang_arr, loc=2)
  pyplot.axis([0, 1024, 0, y_max])
  pyplot.ylabel("Average Execution Time (seconds)")
  pyplot.xlabel("Array Size (N, for matrix of size NxN)")
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
  results = {}

  for mode in trials:
    for sizes in trials[mode]['sizes']:
      for iosizes in trials[mode]['iosizes']:
        if args.concurrent:
          for threads in trials[mode]['threads']:
            tests.append([mode, sizes, iosizes, threads])
        else:
          tests.append([mode, sizes, iosizes, "1"])


  for test in tests:
    res = run_iotest(args.trials, test)
    test_name = " ".join(test)
    results[test_name] = res

  with open(args.outfile, "w") as f:
    json.dump(results, f, sort_keys=False, indent=4, separators=(",", ": "))



def do_graph(args):
  print("GRAPH")


def main():
  # Parse arguments, have a nice help message, and generally pretend that I know
  # how to write good software.
  parser = argparse.ArgumentParser(
      description="Run a set of benchmarks and aggregate the results.")

  parser.add_argument("-o, --output", metavar="FILE", required=True,
                      dest="outfile",
                      help="Destination file for command output.")

  subparser = parser.add_subparsers(help="sub-command help")

  parser_run = subparser.add_parser("run", help="run help")
  parser_run.add_argument("trials", type=int,
                          help="Number of trials to run per test")
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
