import subprocess
import sys

with open(str(sys.argv[2]), "a+") as stdout:
    for i in range(1000):
        p = subprocess.run(["sys161", "kernel", str(sys.argv[1])], cwd="../root", stdout=stdout, stderr=stdout)

