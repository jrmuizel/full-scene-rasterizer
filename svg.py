#!/usr/bin/env python
# convert an svg from swfrip to a header file
from xml.dom.minidom import *
import sys

import xml.etree.ElementTree as ET
def mkpt(a):
    [x,y] = a.split(',')
    x = float(x)
    y = float(y)
    return [x,y]

def ptadd(a, b):
    return [a[0] + b[0], a[1]+b[1]]


tree = ET.parse(sys.argv[1])
dom = parse(sys.argv[1])
root = tree.getroot()
#print dom, tree
commands = []
data = []
for i in root.findall(".//path"):
    properties = dict((key.strip(),value.strip()) for (key, value) in [prop.strip().split(":") for prop in i.attrib["style"].strip().split(';') if prop])
    path = i.attrib["d"]
    #print properties
    if properties["fill"] != "none":
        color = int(properties["fill"].strip("#"),16)
        #print "fill", ((color >> 16)&0xff)/255., ((color>>8)&0xff)/255., (color&0xff)/255., 1.0
        commands += ['f']
        data += [((color >> 16)&0xff)/255., (((color>>8)&0xff)/255.), ((color&0xff)/255.), (1.0)]
        path_data = path.split()
        #print path_data
        j = 0
        cur = [0,0]
        while j < len(path_data):
            d = path_data[j]
            j += 1
            if d == 'M':
                #print 'M', path_data[j].split(',')
                commands += ['m']
                data += mkpt(path_data[j])
                cur = mkpt(path_data[j])
                j += 1
            elif d == 'L':
                #print 'l', path_data[j].split(',')
                commands += ['l']
                data += mkpt(path_data[j])
                cur = mkpt(path_data[j])
                j += 1
            elif d == 'l':
                #print 'l', cur,ptadd(cur, mkpt(path_data[j]))
                commands += ['l']
                data += ptadd(cur, mkpt(path_data[j]))
                cur = ptadd(cur, mkpt(path_data[j]))
                j += 1
            elif d == 'q':
                #print 'q', path_data[j].split(','), path_data[j+1].split(',')
                commands += ['c']
                data += ptadd(cur, mkpt(path_data[j]))
                data += ptadd(cur, mkpt(path_data[j+1]))
                cur = ptadd(cur, mkpt(path_data[j+1]))
                j += 2
            else:
                print "ABORT"
                assert(0)
    #print [prop.strip().split(":") for prop in i.attrib["style"].strip().split(';') if prop], i.attrib["d"]
    #print [prop for prop in i.attrib["style"].strip().split(';') if prop], i.attrib["d"]
    #print i.attrib["style"].strip()

print "double data[] = {" + ",".join([str(i) for i in data]) + "};"
print "char commands[] = {" + ",".join(['\'' + i + '\'' for i in commands]) + "};"
