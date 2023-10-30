import sys, os, getopt, sniper_lib, sniper_stats
import collections

def usage():
        print('Usage: ', sys.argv[0], '-d <root directory of outputs (default: .)> -l <line from the outputs you want value of>')

rootdir = ''
targetline = ''

def make_result_list(key, value):
    pass
try:
    opts, args = getopt.getopt(sys.argv[1:], 'd:l:')
except(getopt.GetoptError, r):
        print(e)
        usage()
        sys.exit()

for o, a in opts:
    if o == '-d':
        rootdir = a
    if o == '-l':
        targetline = a

finallist = []
for root, dirs, files in os.walk(rootdir):
    #list_subfolders_with_paths = [f.path for f in scandir.scandir(rootdir) if f.is_dir()]
    for subf in dirs:
        try:
            subfog = subf[:-2]
            subf = rootdir+subf
            #print "Working on "+subf
            results = sniper_lib.get_results(0, subf, partial = None)
            with sniper_lib.OutputToLess():
                for key, value in sorted(results['results'].items(), key = lambda(key, value): key.lower()):
                    if type(value) is dict:
                        for _key, _value in sorted(value.items()):
                            if key+"."+_key == targetline:
                                finallist.append(subfog + " " + _value)
                    elif type(value) is list and key == targetline:
                        finallist.append(subfog + " " + ','.join(map(str, value)))
                    elif key ==targetline:
                        finallist.append(subfog + " " + str(value))
        except:
            pass

finaldict ={}
for el in sorted(finallist, key=str.lower):
    key = el.split(" ")[0]
    value = el.split(" ")[1]
    finaldict[key] = finaldict.get(key, 0) + float(value)

for key in collections.OrderedDict(sorted(finaldict.items())):
    print(str(key)+" "+str(int(finaldict[key])))
