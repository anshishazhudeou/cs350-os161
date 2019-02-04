import subprocess
import sys
import random
total_iter = 1000
test_percent = 0.01

with open(str(sys.argv[2]), "a+") as stdout:
    for core in (2, 4):
        for i in range(total_iter):
            p = subprocess.run(["sys161", "-c{}cores_sys161.conf".format(core), "kernel", str(sys.argv[1])], cwd="../root",\
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
            if (random.randint(1, int(total_iter/test_percent)+1) <= total_iter):
                stdout.write(p.stdout if p.returncode == 0 else p.stderr)

