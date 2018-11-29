#!/usr/bin/env python3

import os
import sys
import re
import matplotlib
import numpy as np
import scipy
import itertools
from matplotlib import rc
from docopt import docopt

import matplotlib.pyplot as plt

class Statistic:
    def __init__(self):
        self.data = []

    def append(self, d):
        self.data.append(d)

    def attrib(self, atr):
        if len(self.data) == 0:
            return np.nan
        if atr == "max":
            return max(self.data)
        if atr == "min":
            return min(self.data)
        if atr == "median":
            return np.median(self.data)
        if atr == "mean":
            return np.mean(self.data)
        if atr == "std_dev":
            return np.std(self.data)

class PerRankStats:
    def __init__(self, num):
        self.rank_num = num
        self.rendering = Statistic()
        self.compositing = Statistic()
        self.total = Statistic()
        self.gather = Statistic()
        self.waiting = Statistic()
        self.cpu_per = Statistic()
        self.vmsize = Statistic()
        self.vmrss = Statistic()

    def get_attrib(self, atr):
        if atr == "rendering":
            return self.rendering
        if atr == "compositing":
            return self.compositing
        if atr == "total":
            return self.total
        if atr == "gather":
            return self.gather
        if atr == "waiting":
            return self.waiting
        if atr == "cpu_per":
            return self.cpu_per
        if atr == "vmsize":
            return self.vmsize
        if atr == "vmrss":
            return self.vmrss

class BenchmarkRun:
    def __init__(self, compositor, node_count):
        self.compositor = compositor
        self.node_count = node_count
        self.max = np.nan
        self.min = np.nan
        self.median = np.nan
        self.median_abs_dev = np.nan
        self.mean = np.nan
        self.std_dev = np.nan
        self.compositing_overhead = Statistic()
        self.local_max_render_time = Statistic()

        if compositor == "icet":
            self.icet_composite_time = Statistic()
        else:
            self.icet_composite_time = None

        self.frame_times = Statistic()
        self.rank_data = {}

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

# How is this not a built-in function!?
def first_true(iterable, default=False, pred=None):
    return next(filter(pred, iterable), default)

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

    def get_run(self, compositor, nodes):
        if compositor == "ospray":
            return first_true(self.ospray, None,
                    lambda x: x.node_count == nodes)
        elif compositor == "icet":
            return first_true(self.icet, None,
                    lambda x: x.node_count == nodes)

def filter_nans(y):
    return list(filter(lambda v: np.isfinite(v), y))

def filter_xy_nans(x, y):
    for i in range(0, len(y)):
        if np.isnan(y[i]):
            x[i] = np.nan

    return (filter_nans(x), filter_nans(y))

doc = """Plot Scaling

Usage:
    plot_scaling.py <var> <title> <file>... [options]

Options:
    -o OUTPUT              save the plot to an output file
    --min <value>          min node count threshold to plot
    --std-dev              plot std. dev as error bars
    --ranks <ranks>...     plot per-rank data about just the subset of ranks (or all).
                           Put the ranks in quotes because docopt is stupid.
    --rank-var <var>       plot this variable about the ranks
    --overall              also show overall frame time on the rank data plot
    --breakdown            plot the overall compositing and rendering performance breakdown
"""
args = docopt(doc)

parse_fname = re.compile("bench_(\w+)_(\d+)n_(\d+)x(\d+)-(?:.*-)?\d+.*\.txt")
parse_rank_file = re.compile("rank(\d+)")
parse_max = re.compile("max: (\d+)")
parse_min = re.compile("min: (\d+)")
parse_median = re.compile("median: (\d+)")
parse_median_abs_dev = re.compile("median abs dev: (\d+)")
parse_mean = re.compile("mean: (\d+)")
parse_std_dev = re.compile("std dev: (\d+)")
parse_compositing_overhead = re.compile("Compositing overhead: (\d+)")
parse_local_max_render_time = re.compile("Max render time: (\d+)")
parse_icet_composite_time = re.compile("Frame: \d+ IceT composite time: (\d+(?:\.\d+)?)")
parse_frame_time = re.compile("Frame: \d+ took: (\d+)")

plot_var = args["<var>"]
title = args["<title>"]
min_node_count = -1
if args["--min"]:
    min_node_count = int(args["--min"])

if args["-o"] and os.path.splitext(args["-o"])[1] == ".pdf":
    #rc('font',**{'family':'sans-serif','sans-serif':['Helvetica']})
    ## for Palatino and other serif fonts use:
    rc('font',**{'family':'serif','serif':['Palatino']})
    rc('text', usetex=True)

ax = plt.subplot(111)

scaling_runs = {}

def plot_scaling_set():
    ax.set_xscale("log", basex=2, nonposx="clip")
    #ax.set_yscale("log", basey=2, nonposy="clip")

    for res,series in scaling_runs.items():
        x = list(map(lambda r: r.node_count, series.icet))
        y = list(map(lambda r: r.attrib(plot_var), series.icet))

        if args["--breakdown"]:
            y = list(map(lambda r: r.local_max_render_time.attrib(plot_var), series.icet))

        (x, y) = filter_xy_nans(x, y)
        if len(y) > 0:
            yerr = list(map(lambda r: r.std_dev, series.icet))
            y_overhead = list(map(lambda r: r.icet_composite_time.attrib(plot_var), series.icet))
            y_overhead = filter_nans(y_overhead)

            # TODO: Wrap this up into a function, and have it average
            # the multiple runs together
            unique_x = [x[0]]
            unique_y = [y[0]]
            unique_yerr = [yerr[0]]
            unique_yoverhead = [y_overhead[0]]
            for a, b, err, overh in zip(x, y, yerr, y_overhead):
                if unique_x[-1] == a:
                    unique_y[-1] = min(unique_y[-1], b)
                    unique_yoverhead[-1] = min(unique_yoverhead[-1], overh)
                else:
                    unique_x.append(a)
                    unique_y.append(b)
                    unique_yerr.append(err)
                    unique_yoverhead.append(overh)

            x = unique_x
            y = unique_y
            yerr = unique_yerr
            y_overhead = unique_yoverhead

            if args["--std-dev"]:
                plt.errorbar(x, y, fmt="o-", label="IceT {}".format(res), linewidth=2, yerr=yerr)
            else:
                plt.plot(x, y, "o-", label="IceT {}".format(res), linewidth=2)
                if args["--breakdown"]:
                    plt.plot(x, y_overhead, "o--", label="IceT Overhead {}".format(res), linewidth=2)


        x = list(map(lambda r: r.node_count, series.ospray))
        y = list(map(lambda r: r.attrib(plot_var), series.ospray))
        if args["--breakdown"]:
            y = list(map(lambda r: r.local_max_render_time.attrib(plot_var), series.ospray))
        (x, y) = filter_xy_nans(x, y)

        yerr = list(map(lambda r: r.std_dev, series.ospray))
        yerr = filter_nans(yerr)
        y_overhead = list(map(lambda r: r.compositing_overhead.attrib(plot_var), series.ospray))
        y_overhead = filter_nans(y_overhead)
        unique_x = [x[0]]
        unique_y = [y[0]]
        unique_yerr = [yerr[0]]
        unique_yoverhead = [y_overhead[0]]
        for a, b, err, overh in zip(x, y, yerr, y_overhead):
            if unique_x[-1] == a:
                unique_y[-1] = min(unique_y[-1], b)
                unique_yoverhead[-1] = min(unique_yoverhead[-1], overh)
            else:
                unique_x.append(a)
                unique_y.append(b)
                unique_yerr.append(err)
                unique_yoverhead.append(overh)

        x = unique_x
        y = unique_y
        yerr = unique_yerr
        y_overhead = unique_yoverhead


        if args["--std-dev"]:
            plt.errorbar(x, y, fmt="o-", label="OSPRay {}".format(res), linewidth=2, yerr=yerr)
        else:
            plt.plot(x, y, "o-", label="OSPRay {}".format(res), linewidth=2)
            if args["--breakdown"]:
                plt.plot(x, y_overhead, "o--", label="OSPRay Overhead {}".format(res), linewidth=2)

    ax.get_xaxis().set_major_formatter(matplotlib.ticker.FormatStrFormatter("%d"))
    plt.title(title)
    plt.ylabel("Time (ms)")
    plt.xlabel("Nodes")
    plt.legend(loc=0)

def plot_rank_data():
    rank_subset = None
    if args["--ranks"]:
        rank_subset = []
        match_range = re.compile("(\d+)-(\d+)")
        for x in args["--ranks"].split():
            m = match_range.match(x)
            if m:
                for i in range(int(m.group(1)), int(m.group(2)) + 1):
                    rank_subset.append(int(i))
            else:
                rank_subset.append(int(x))

    plt.figure(figsize=(28,10))
    for res,series in scaling_runs.items():
        for br in series.ospray:
            for n,rank in br.rank_data.items():
                if rank_subset and not n in rank_subset:
                    continue

                y = rank.get_attrib(args["--rank-var"]).data
                style = "-"
                if rank.rank_num == 0:
                    style = "--"
                plt.plot(list(range(0, len(y))), y, style,
                        label="Rank {}/{} @ {}".format(n, br.node_count, res), linewidth=2)

            if args["--overall"]:
                plt.plot(list(range(0, len(br.frame_times.data))), br.frame_times.data,
                        "--", label="Overall {} @ {}".format(br.node_count, res), linewidth=4)

    plt.title("{} per-rank {} data".format(args["--rank-var"], title))
    if args["--rank-var"] == "cpu_per":
        plt.ylabel("CPU %")
    elif args["--rank-var"] == "vmsize" or args["--rank-var"] == "vmrss":
        plt.ylabel("Memory (MB)")
    else:
        plt.ylabel("Time (ms)")
    plt.xlabel("Frame")
    #plt.legend(loc=0)

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
                    run.compositing_overhead.append(int(m.group(1)))
                    continue
                m = parse_local_max_render_time.search(l)
                if m:
                    run.local_max_render_time.append(int(m.group(1)))
                    continue
                m = parse_frame_time.search(l)
                if m:
                    run.frame_times.append(int(m.group(1)))
                    continue
                m = parse_icet_composite_time.search(l)
                if m:
                    run.icet_composite_time.append(float(m.group(1)))

        scaling_runs[resolution].add_run(run)

parse_rank_rendering = re.compile("Rendering: (\d+)")
parse_rank_compositing = re.compile("Compositing: (\d+)")
parse_rank_total = re.compile("Total: (\d+)")
parse_rank_gather = re.compile("Gather time: (\d+(?:\.\d+)?)")
parse_rank_waiting = re.compile("Waiting for frame: (\d+(?:\.\d+)?)")
parse_rank_cpu_per = re.compile("CPU: (\d+(?:\.\d+)?)")
parse_rank_vmsize = re.compile("VmSize:[^\d]+(\d+)")
parse_rank_vmrss = re.compile("VmRSS:[^\d]+(\d+)")

# Now go through and parse all the per-rank data
if args["--rank-var"]:
    for f in args["<file>"]:
        m = parse_fname.search(f)
        m2 = parse_rank_file.search(f)
        # We have to be careful here because regex sucks
        if m and m2:
            compositor = m.group(1)
            resolution = "{}x{}".format(m.group(3), m.group(4))
            node_count = int(m.group(2))
            rank_num = int(m2.group(1))

            if node_count < min_node_count:
                continue

            run = scaling_runs[resolution].get_run(compositor, node_count)
            if not run:
                print("Failed to find base run for rank log file {}!?".format(f))

            rank = PerRankStats(rank_num)
            run.rank_data[rank_num] = rank

            with open(f, 'r') as content:
                for l in content:
                    m = parse_rank_rendering.search(l)
                    if m:
                        rank.rendering.append(int(m.group(1)))
                        continue
                    m = parse_rank_compositing.search(l)
                    if m:
                        rank.compositing.append(int(m.group(1)))
                        continue
                    m = parse_rank_total.search(l)
                    if m:
                        rank.total.append(int(m.group(1)))
                        continue
                    m = parse_rank_gather.search(l)
                    if m:
                        rank.gather.append(float(m.group(1)))
                        continue
                    m = parse_rank_waiting.search(l)
                    if m:
                        rank.waiting.append(float(m.group(1)))
                        continue
                    m = parse_rank_cpu_per.search(l)
                    if m:
                        rank.cpu_per.append(float(m.group(1)))
                        continue
                    m = parse_rank_vmsize.search(l)
                    if m:
                        rank.vmsize.append(float(m.group(1)) * 0.001)
                        continue
                    m = parse_rank_vmrss.search(l)
                    if m:
                        rank.vmrss.append(float(m.group(1)) * 0.001)

for res,series in scaling_runs.items():
    series.icet.sort(key=lambda r: r.node_count)
    series.ospray.sort(key=lambda r: r.node_count)

# To adjust the fig size if we want
#plt.figure(figsize=(8,3.5))

if args["--rank-var"]:
    plot_rank_data()
else:
    plot_scaling_set()

ax.spines["right"].set_visible(False)
ax.spines["top"].set_visible(False)
ax.yaxis.set_ticks_position("left")
ax.xaxis.set_ticks_position("bottom")

if args["-o"]:
    if os.path.splitext(args["-o"])[1] == ".png":
        plt.savefig(args["-o"], dpi=150, bbox_inches="tight")
    else:
        plt.savefig(args["-o"], bbox_inches="tight")
    print("saved to {}".format(args["-o"]))
else:
    plt.show()

