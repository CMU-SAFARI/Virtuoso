import sys
import re
import collections

# first argument = frequency
# second argument = number of cores

f1 = open("f1", "r")
f2 = open("f2", "r")
f3 = open("f3", "r")
f4 = open("f4", "r")
f5 = open("f5", "r")

lines1 = f1.readlines()
lines2 = f2.readlines()
lines3 = f3.readlines()
lines4 = f4.readlines()

lines5 = f5.readlines()


frequency = float(sys.argv[1])
results = {}
averages = {}

l1per = {}
l2per = {}
l3per = {}
l4per = {}
for i in range(0, len(lines1)):
    s1 = lines1[i].split(" ")[1]
    s2 = lines2[i].split(" ")[1]
    s3 = lines3[i].split(" ")[1]
    s4 = lines4[i].split(" ")[1]
    s5 = lines5[i].split(" ")[1]
    name1 = lines1[i].split(" ")[0]
    name2 = lines1[i].split(" ")[0]
    name3 = lines1[i].split(" ")[0]
    name4 = lines1[i].split(" ")[0]
    name = name1

    if name1 != name2 or name1 != name3 or name1 != name4:
        print("EXPERIMENT INPUT FILES ARE NOT IN SAME ORDER ABORT ABORT")
        exit(1)

    total = 0
    cnt = 0
    #how many cores...
    for j in range(0, int(sys.argv[2])):
        total += int(str(s1).split(",")[j])
        total += int(str(s2).split(",")[j])
        total += int(str(s3).split(",")[j])
        total += int(str(s4).split(",")[j])

        cnt += int(str(s5).split(",")[j])
        #print cnt

    l1per[name] = float(int(str(s1).split(",")[j]))/float(total)
    l2per[name] = float(int(str(s2).split(",")[j]))/float(total)
    l3per[name] = float(int(str(s3).split(",")[j]))/float(total)
    l4per[name] = float(int(str(s4).split(",")[j]))/float(total)

    if name in averages:
        averages[name] += (int(total)/(1000000/frequency))/cnt #uncomment for average
    else:
        averages[name] = (int(total)/(1000000/frequency))/cnt #uncomment for average

    if name in results:
        results[name] += (int(total)/(1000000/frequency))
    else:
        results[name] = (int(total)/(1000000/frequency))


print("Total:")
for key in collections.OrderedDict(sorted(results.items())):
    print key+" "+str(results[key])

print("Averages:")
for key in collections.OrderedDict(sorted(averages.items())):
    print key+" "+str(averages[key])

print("Percentages per level:")
print("Level1:")
for key in collections.OrderedDict(sorted(l1per.items())):
    print key+" "+str(l1per[key])

print("Level2:")
for key in collections.OrderedDict(sorted(l2per.items())):
    print key+" "+str(l2per[key])

print("Level3:")
for key in collections.OrderedDict(sorted(l3per.items())):
    print key+" "+str(l3per[key])

print("Level4:")
for key in collections.OrderedDict(sorted(l4per.items())):
    print key+" "+str(l4per[key])

f1.close()
f2.close()
f3.close()
f4.close()
f5.close()
