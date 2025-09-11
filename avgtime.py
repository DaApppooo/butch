#!/bin/env python3
import subprocess as sp
from tqdm import tqdm
from sys import exit

C = 100_000
t = 0.0
for _ in tqdm(range(C)):
  r = sp.run(['./readtest', 'test.butch', 'test.code'], capture_output=True)
  if r.returncode != 0:
    print("ERROR OCCURED, FULL OUTPUT:")
    print(r.stdout.decode('ascii'))
    exit(1)
  T = r.stdout.decode('ascii').rsplit('\n', 2)[-2].rsplit(' ',2)[-2]
  t += float(T)
print(f"{C} took {t}, avg={t/C}")
