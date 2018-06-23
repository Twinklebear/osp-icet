#!/usr/bin/env python3

import os
import sys
import re
import matplotlib
import numpy
import scipy
from docopt import docopt

import matplotlib.pyplot as plt

class Statistic:
    def __init__(self):
        self.data = []

    def attrib(self, atr):
        if atr == "max":
            return max(self.data)
        if atr == "min":
            return min(self.data)
        if atr == "median":
            return numpy.median(self.data)
        if atr == "mean":
            return numpy.mean(self.data)
        if atr == "std_dev":
            return numpy.std(self.data)


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
        self.compositing_overhead = Statistic()
        self.local_max_render_time = Statistic()

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
    plot_scaling.py <var> <machine> <file>... [options]

Options:
    -o OUTPUT     save the plot to an output file
    --min <value> min node count threshold to plot
    --std-dev     plot std. dev as error bars
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
parse_compositing_overhead = re.compile("Compositing overhead: (\d+)")
parse_local_max_render_time = re.compile("Max render time: (\d+)")

plot_var = args["<var>"]
machine = args["<machine>"]
min_node_count = -1
if args["--min"]:
    min_node_count = int(args["--min"])

scaling_runs = {}

for f in args["<file>"]:
    m = parse_fname.search(f)
    # We have to be careful here because regex sucks
    if m and not parse_rank_file.search(f):
        print("Parsing run log {}".format(f))
        resolution = "{}x{}".format(m.group(3), m.group(4))
        node_count = int(m.group(2))

        if node_count < min_node_count:
            continue

        run = BenchmarkRun(m.group(1), node_count)
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

                m = parse_compositing_overhead.search(l)
                if m:
                    run.compositing_overhead.data.append(int(m.group(1)))

                m = parse_local_max_render_time.search(l)
                if m:
                    run.local_max_render_time.data.append(int(m.group(1)))

        scaling_runs[resolution].add_run(run)

ax = plt.subplot(111)
ax.set_xscale("log", basex=2, nonposx="clip")
#ax.set_yscale("log", basey=2, nonposy="clip")

for res,series in scaling_runs.items():
    series.icet.sort(key=lambda r: r.node_count)
    series.ospray.sort(key=lambda r: r.node_count)

    x = list(map(lambda r: r.node_count, series.icet))
    #y = list(map(lambda r: r.attrib(plot_var), series.icet))
    y_overhead = list(map(lambda r: r.attrib(plot_var) - r.local_max_render_time.attrib(plot_var), series.icet))
    y = list(map(lambda r: r.local_max_render_time.attrib(plot_var), series.icet))
    if args["--std-dev"]:
        yerr = list(map(lambda r: r.std_dev, series.icet))
        plt.errorbar(x, y, fmt="o-", label="IceT {}".format(res), linewidth=2, yerr=yerr)
    else:
        plt.plot(x, y, "o-", label="IceT {}".format(res), linewidth=2)
        plt.plot(x, y_overhead, "o--", label="IceT Overhead {}".format(res), linewidth=2)

    x = list(map(lambda r: r.node_count, series.ospray))
    #y = list(map(lambda r: r.attrib(plot_var), series.ospray))
    y_overhead = list(map(lambda r: r.compositing_overhead.attrib(plot_var), series.ospray))
    y = list(map(lambda r: r.local_max_render_time.attrib(plot_var), series.ospray))
    if args["--std-dev"]:
        yerr = list(map(lambda r: r.std_dev, series.ospray))
        plt.errorbar(x, y, fmt="o-", label="OSPRay {}".format(res), linewidth=2, yerr=yerr)
    else:
        plt.plot(x, y, "o-", label="OSPRay {}".format(res), linewidth=2)
        plt.plot(x, y_overhead, "o--", label="OSPRay Overhead {}".format(res), linewidth=2)

ax.get_xaxis().set_major_formatter(matplotlib.ticker.FormatStrFormatter("%d"))
plt.title("Scaling Runs on {} ({} time)".format(machine, plot_var))
plt.ylabel("Time (ms)")
plt.xlabel("Nodes")
plt.legend(loc=0)
if args["-o"]:
    plt.savefig(args["-o"])
else:
    plt.show()

