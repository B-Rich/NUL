#!/usr/bin/env python

import sys
import re
import os
import os.path
import string
import time
import numpy as np

class Axis:
    def __init__(self, name):
        self.name = name
        self.units = None
        self.num = None
    def getLabel(self):
        if self.units:
            return "%s [%s]" % (self.name, self.units)
        else:
            return self.name

class Column:
    def __init__(self, name, units, axis):
        self.name = name
        self.units = units
        self.axis = axis

class Row(dict):
    def __init__(self, graph, date):
        self.graph = graph
        self.date = date
    def __getitem__(self, column):
        try:
            return dict.__getitem__(self, column)
        except KeyError:
            return None
    def getDate(self):
        d = time.gmtime(time.mktime(self.date))
        return "Date.UTC(%s, %s, %s, %s, %s, %s)" % \
            (d.tm_year, d.tm_mon-1, d.tm_mday, d.tm_hour, d.tm_min, d.tm_sec)

class Graph:
    def __init__(self, id, title):
        self.columns = {}
        self.id = id
        self.title = title
        self.rows = []
        self.date2row = {}
        self.axes = {}

    def __getitem__(self, date):
        try:
            rownum = self.date2row[date]
        except KeyError:
            rownum = len(self.rows)
            self.date2row[date] = rownum
        try:
            return self.rows[rownum]
        except IndexError:
            self.rows[rownum:rownum] = [Row(self, date)]
            return self.rows[rownum]

    def addValue(self, date, col, val, units):
        row = self[date]
        row[col] = val
        if not self.columns.has_key(col):
            axis=self.getAxis(col)
            self.columns[col]=Column(col, units, axis);
        self.columns[col].units=units
        self.columns[col].axis.units=units

    def setAxis(self, col, axis):
        self.columns[col].axis = self.getAxis(axis)

    def getAxis(self, col):
        if not self.axes.has_key(col):
            self.axes[col] = Axis(col)
        return self.axes[col]

    def findRanges(self):
        for axis in self.axes.values():
            cols = [col for col in self.columns.values() if col.axis == axis]
            low = None
            high = None
            all_in_range = True
            for col in cols:
                values = np.array([row[col.name] for row in self.rows if row[col.name] != None], np.float64)
                if low == None and high == None:
                    lastmonth = values[-30:]
                    median = np.median(lastmonth)
                    low  = median * 0.95
                    high = median * 1.05

                if (values > high).any() or (values < low).any():
                    all_in_range = False
            if all_in_range:
                axis.yrange_max = high
                axis.yrange_min = low
            else:
                axis.yrange_max = None
                axis.yrange_min = None

    def fixupAxisNumbers(self):
        axix_keys = self.axes.keys()
        for a in axix_keys:
            axis = self.axes[a]
            cols = [col for col in self.columns.values() if col.axis == axis]
            if len(cols) == 0:
                del(self.axes[a])
        num = 0
        for axis in self.axes.itervalues():
            axis.num = num
            num += 1

    def jschart(self):
        print """
			window.chart = new Highcharts.StockChart({
			    chart: {
			        renderTo: '"""+self.id+"""'
			    },

			    rangeSelector: {
			        selected: 1
			    },

			    title: {
			        text: '"""+self.title+"""'
			    },
                            legend: {
                                enabled: true,
                                floating: false,
                                verticalAlign: "top",
                                x: 100,
                                y: 60,
                            },
                            tooltip: {
                                formatter: function() {
                                    var s = '<b>'+ Highcharts.dateFormat('%a, %d %b %Y %H:%M:%S', this.x) +'</b><br/>';
                                    s += commitMap[this.x].msg;
                                    $.each(this.points, function(i, point) {
                                        s += '<br/><span style="color:'+ point.series.color+';">'+ point.series.name +'</span>: '+point.y;
                                    });
                                    return s;
                                }
                            },
			    xAxis: {
			        maxZoom: 14 * 24 * 3600000 // fourteen days
			    },"""
	print  """
		            plotOptions: {
		                series: {
		                    events: {
		                        click: function(event) {
					    var lastpoint = null;
					    for (var i in this.data) {
					      if (event.point == this.data[i]) {
					        if (i > 0) lastpoint = this.data[i-1];
					        break;
					      }
					    }
					    if (lastpoint)
					      window.location = "http://os.inf.tu-dresden.de/~jsteckli/cgi-bin/cgit.cgi/nul/log/?qt=range&q="+commitMap[lastpoint.x].hash+'..'+commitMap[event.point.x].hash;
					    else
					      window.location = "http://os.inf.tu-dresden.de/~jsteckli/cgi-bin/cgit.cgi/nul/log/?id="+commitMap[event.point.x].hash;
					}
		                    }
		                }
		            },
			    yAxis: ["""
	for axis in self.axes.values():
            print "\t\t\t\t{"
            print "\t\t\t\t\tlineWidth: 1,"
            print "\t\t\t\t\tlabels: { align: 'right', x: -3 },"
            print "\t\t\t\t\ttitle: { text: '%s' }," % axis.getLabel()
            #print "\t\t\t\t\tplotBands: { from: %s, to: %s, color: '#eee' }," % (col.low, col.high)
            if axis.yrange_min: print "\t\t\t\t\tmin: %s," % axis.yrange_min
            if axis.yrange_max: print "\t\t\t\t\tmax: %s," % axis.yrange_max
            print "\t\t\t\t},"
        print """\t\t\t    ],

			    series: ["""
        num = 0
	for col in self.columns.values():
            print "\t\t\t\t{ name: '%s [%s]', yAxis: %d, data: [" % (col.name, col.units, col.axis.num)
            num += 1
            for row in self.rows:
                val = row[col.name]
                if val == None: val = "null"
                print "\t\t\t\t\t[%s, %s], " % (row.getDate(), val)
            print "\t\t\t\t]},"
        print """\t\t\t    ],
			});"""

class Graphs(dict):
    pass

graphs = Graphs()
commits = {}

re_date = re.compile('^Date: (.*)')
re_testing = re.compile('^(\([0-9]+\) (#   )?)?\s*Testing "(.*)" in (.*):\s*$')
re_commit = re.compile('(\S+) (.*?), commit: (.*)')
re_commithash = re.compile('([0-9a-f]{7})(-dirty)? \(')
re_check = re.compile('^(\([0-9]+\) (#   )?)?!\s*(.*?)\s+(\S+)\s*$')
re_perf =  re.compile('^(\([0-9]+\) (#   )?)?!\s*(.*?)\s+PERF:\s*(.*?)\s+(\S+)\s*$')
re_perfaxis = re.compile('axis="([^"]+)"')

date = time.localtime(time.time())

for line in sys.stdin.readlines():
    line = line.rstrip()

    match = re_date.match(line)
    if (match):
        date = time.strptime(match.group(1), "%a, %d %b %Y %H:%M:%S +0200")
        continue

    match = re_testing.match(line)
    if match:
        what = match.group(3)
        where = match.group(4)

        match = re_commit.match(what)
        if match:
            date = time.strptime(match.group(2), "%Y-%m-%d %H:%M:%S")
            commit = match.group(3)
            match = re_commithash.search(commit);
            if match:
                commithash = match.group(1)
            else:
                commithash = None
            commits[date] = (commit, commithash)

        (basename, ext) = os.path.splitext(os.path.basename(where))

        if what != "all": title = what
        else: title = basename
        try:
            graph = graphs[basename]
        except KeyError:
            graph = Graph(basename, title)
            graphs[basename] = graph
        continue

    match = re_perf.match(line)
    if match:
        perfstr = match.group(4)
        perf = perfstr.split()
        col = perf[0]
        try:
            val = float(perf[1])
        except ValueError:
            val = None
        try:
            units = perf[2]
        except:
            units = None

        graph.addValue(date, col, val, units)

        match = re_perfaxis.search(perfstr)
        if match:
            graph.setAxis(col, match.group(1));

graphs = [g for g in graphs.values() if len(g.columns)]
graphs = sorted(graphs, key=lambda g: g.title.lower())

for g in graphs:
    g.findRanges()
    g.fixupAxisNumbers()

print """
<!DOCTYPE HTML>
<html>
    <head>
	<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
	<title>NUL Performance Plots</title>

	<script type="text/javascript" src="http://ajax.googleapis.com/ajax/libs/jquery/1.6.1/jquery.min.js"></script>
	<script type="text/javascript">
		var commitMap = {"""
for d in sorted(commits.iterkeys()):
    v = commits[d];
    print '\t\t\t%d: { msg: "%s", hash: "%s" },' % (1000*time.mktime(d), v[0].replace('"', '\\"'), str(v[1]).replace('"', '\\"'))
print """\t\t};
		$(function() {"""
for graph in graphs:
    graph.jschart()
print """
		});
	</script>
    </head>

    <body>
	<h1>NUL Performance Plots</h1>
	<script type="text/javascript" src="js/highstock.js"></script>
        <ul>
"""
for graph in graphs:
    print "	<li><a href='#%s'>%s</a></li>" % (graph.title, graph.title)
print "    </ul>"
for graph in graphs:
    print "	<h2><a name='%s'>%s</a></h2>" % (graph.title, graph.title)
    print '	<div id="%s" style="height: 400px"></div>' % graph.id
print """
    </body>
</html>
"""

# Local Variables:
# compile-command: "cat nul-nightly/nul_*.log|./wvperfpreprocess.py|./wvperf2html.py > graphs.html"
# End:
