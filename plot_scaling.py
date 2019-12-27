#!/usr/bin/env python3

import os
import sys
import re
import matplotlib
import numpy as np
import scipy
from matplotlib import rc
from matplotlib import cm
from docopt import docopt
import matplotlib.pyplot as plt

class BenchmarkRun:
    def __init__(self, compositor, node_count):
        self.compositor = compositor
        self.node_count = node_count
        self.compositing_overhead = []
        self.frame_times = []

class ScalingRun:
    def __init__(self, res):
        self.resolution = res
        self.icet = []
        self.dfb = []

    def add_run(self, run):
        if run.compositor == "icet":
            self.icet.append(run)
        elif run.compositor == "dfb":
            self.dfb.append(run)

    def sort(self):
        self.icet.sort(key=lambda r: r.node_count)
        self.dfb.sort(key=lambda r: r.node_count)

    def get_results(self, compositor, var):
        x = []
        y = []
        yerr = []
        data = self.dfb
        if compositor == "icet":
            data = self.icet

        for run in data:
            x.append(run.node_count)
            if var == "total":
                y.append(np.mean(run.frame_times))
                yerr.append(np.std(run.frame_times))
            elif var == "compositing":
                y.append(np.mean(run.compositing_overhead))
                yerr.append(np.std(run.compositing_overhead))
            else:
                print("Unrecognized data var {}".format(var))
        return x, y, yerr


doc = """Plot Scaling

Usage:
    plot_scaling.py <var> <title> <file>... [options]

Options:
    -o OUTPUT              save the plot to an output file
"""
args = docopt(doc)

parse_log_fname = re.compile(".*bench-(\w+)-(\d+)n-([^0-9]+)-(\d+)\.txt")
match_compositing_overhead = re.compile("(\w+) Compositing Overhead: (\d+\.?\d*)ms")
match_frame_time = re.compile("Frame (\d+) took (\d+)ms")

scaling_run = ScalingRun("1024x1024")
for filename in args["<file>"]:
    m_config = parse_log_fname.match(filename)
    if not m_config:
        print("Unrecognized filename pattern {}".format(filename))
        sys.exit(1)

    print(m_config.groups())
    with open(filename, "r") as f:
        run = BenchmarkRun(m_config.group(1), int(m_config.group(2)))
        for l in f:
            m = match_compositing_overhead.search(l)
            if m:
                run.compositing_overhead.append(float(m.group(2)))
                continue
            m = match_frame_time.search(l)
            if m:
                run.frame_times.append(float(m.group(2)))
                continue
        scaling_run.add_run(run)

scaling_run.sort()

if args["-o"] and os.path.splitext(args["-o"])[1] == ".pdf":
    #rc('font',**{'family':'sans-serif','sans-serif':['Helvetica']})
    ## for Palatino and other serif fonts use:
    rc('font',**{'family':'serif','serif':['Palatino']})
    rc('text', usetex=True)

fig, ax = plt.subplots()

plot_var = args["<var>"]

ax.set_xscale("log", basex=2, nonposx="clip")
#ax.set_yscale("log", basey=2, nonposy="clip")

x, y, yerr = scaling_run.get_results("dfb", plot_var)
plt.plot(x, y, label="DFB")

x, y, yerr = scaling_run.get_results("icet", plot_var)
plt.plot(x, y, label="IceT")

plt.title(args["<title>"])
plt.legend()

ax.spines["right"].set_visible(False)
ax.spines["top"].set_visible(False)
ax.yaxis.set_ticks_position("left")
ax.xaxis.set_ticks_position("bottom")
ax.xaxis.set_major_formatter(matplotlib.ticker.FormatStrFormatter("%d"))

if args["-o"]:
    if os.path.splitext(args["-o"])[1] == ".png":
        plt.savefig(args["-o"], dpi=150, bbox_inches="tight")
    else:
        plt.savefig(args["-o"], bbox_inches="tight")
    print("saved to {}".format(args["-o"]))
else:
    plt.show()

