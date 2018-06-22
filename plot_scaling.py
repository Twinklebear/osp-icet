#!/usr/bin/env python3

import os
import sys
import re
import matplotlib
from docopt import docopt

import matplotlib.pyplot as plt

class BenchmarkRun:
    def __init__(self, compositor, node_count):
        self.compositor = compositor
        self.node_count = node_count
        self.max = 0
        self.min = 0
        self.median = 0
        self.median_abs_dev = 0
        self.mean = 0
        self.std_dev = 0

    def attrib(self, attrib):
        if attrib == "max":
            return self.max
        if attrib == "min":
            return self.min
        if attrib == "median":
            return self.median
        if attrib == "median_abs_dev":
            return self.median_abs_dev
        if attrib == "mean":
            return self.mean
        if attrib == "std_dev":
            return self.std_dev

    def __str__(self):
        return """BenchmarkRun:
        compositor = {}
        node_count = {}
        max = {}
        min = {}
        median = {}
        median abs dev = {}
        mean = {}
        std dev = {}
        """.format(self.compositor,
                self.node_count, self.max, self.min,
                self.median, self.median_abs_dev,
                self.mean, self.std_dev)

class ScalingRun:
    def __init__(self, res):
        self.resolution = res
        self.icet = []
        self.ospray = []

    def add_run(self, run):
        if run.compositor == "icet":
            self.icet.append(run)
        elif run.compositor == "ospray":
            self.ospray.append(run)

doc = """Plot Scaling

Usage:
    plot_scaling.py <var> <machine> <file>... [-o OUTPUT]

Options:
    -o OUTPUT    save the plot to an output file
"""
args = docopt(doc)

parse_fname = re.compile("bench_(\w+)_(\d+)n_(\d+)x(\d+)-(?:.*-)?\d+.*\.txt")
parse_rank_file = re.compile("rank\d+")
parse_max = re.compile("max: (\d+)")
parse_min = re.compile("min: (\d+)")
parse_median = re.compile("median: (\d+)")
parse_median_abs_dev = re.compile("median abs dev: (\d+)")
parse_mean = re.compile("mean: (\d+)")
parse_std_dev = re.compile("std dev: (\d+)")

plot_var = args["<var>"]
machine = args["<machine>"]
scaling_runs = {}

for f in args["<file>"]:
    m = parse_fname.search(f)
    # We have to be careful here because regex sucks
    if m and not parse_rank_file.search(f):
        print("Parsing run log {}".format(f))
        resolution = "{}x{}".format(m.group(3), m.group(4))

        run = BenchmarkRun(m.group(1), int(m.group(2)))
        if not resolution in scaling_runs:
            scaling_runs[resolution] = ScalingRun(resolution)

        with open(f, 'r') as content:
            for l in content:
                m = parse_max.search(l)
                if m:
                    run.max = int(m.group(1))
                    continue

                m = parse_min.search(l)
                if m:
                    run.min = int(m.group(1))
                    continue

                m = parse_median.search(l)
                if m:
                    run.median = int(m.group(1))
                    continue

                m = parse_median_abs_dev.search(l)
                if m:
                    run.median_abs_dev = int(m.group(1))
                    continue

                m = parse_mean.search(l)
                if m:
                    run.mean = int(m.group(1))
                    continue

                m = parse_std_dev.search(l)
                if m:
                    run.std_dev = int(m.group(1))
                    continue

        scaling_runs[resolution].add_run(run)

ax = plt.subplot(111)
ax.set_xscale("log", basex=2, nonposx="clip")
#ax.set_yscale("log", basey=2, nonposy="clip")

for res,series in scaling_runs.items():
    series.icet.sort(key=lambda r: r.node_count)
    series.ospray.sort(key=lambda r: r.node_count)

    x = list(map(lambda r: r.node_count, series.icet))
    y = list(map(lambda r: r.attrib(plot_var), series.icet))
    if plot_var == "mean":
        yerr = list(map(lambda r: r.std_dev, series.icet))
        plt.errorbar(x, y, fmt="o-", label="IceT {}".format(res), linewidth=2, yerr=yerr)
    else:
        plt.plot(x, y, "o-", label="IceT {}".format(res), linewidth=2)

    x = list(map(lambda r: r.node_count, series.ospray))
    y = list(map(lambda r: r.attrib(plot_var), series.ospray))
    if plot_var == "mean":
        yerr = list(map(lambda r: r.std_dev, series.ospray))
        plt.errorbar(x, y, fmt="o-", label="OSPRay {}".format(res), linewidth=2, yerr=yerr)
    else:
        plt.plot(x, y, "o-", label="OSPRay {}".format(res), linewidth=2)

ax.get_xaxis().set_major_formatter(matplotlib.ticker.FormatStrFormatter("%d"))
plt.title("Scaling Runs on {} ({} time)".format(machine, plot_var))
plt.ylabel("Rendering + Compositing (ms)")
plt.xlabel("Nodes")
plt.legend(loc=0)
if args["-o"]:
    plt.savefig(args["-o"])
else:
    plt.show()

