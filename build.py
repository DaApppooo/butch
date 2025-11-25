#!/bin/env python3
from os import listdir, path, system, remove
from sys import argv
from json import dump
from shlex import split

def fname(s: str) -> str:
  return s.rsplit('/', 1)[1]
def chext(s: str, new: str) -> str:
  return s.rsplit('.',1)[0]+new

SRC = [f'src/{p}' for p in listdir('src') if p.endswith('.cpp')]
OBJ = [chext(p,'.o') for p in SRC]
FLAGS = "-I src"
EXAMPLE_ARGS = "./test.butch ./test.code"
EXAMPLE = "examples/readparse.cpp"
EXAMPLE_BIN = 'bin/' + fname(chext(EXAMPLE,'.out'))
EXAMPLE_CMD = f"{EXAMPLE_BIN} {EXAMPLE_ARGS}"
SRC_ARG = ' '.join(SRC)
cdb = [
  {
    'directory': path.abspath('src'),
    'arguments': ['g++', '-c', i, '-o', chext(i,'.o'), *split(FLAGS)],
    'file': i
  }
  for i in SRC
] + [
  {
    'directory': path.abspath('src'),
    'arguments': ['g++', *SRC, i, '-o', f'bin/{fname(chext(i,'.out'))}', *split(FLAGS)],
    'file': i
  }
  for i in (f'examples/{j}' for j in listdir('examples'))
]
with open('compile_commands.json', 'w') as f:
  dump(cdb, f)

def run(cmd: str, show = True):
  if show:
    print(cmd)
  r = system(cmd)>>8
  if r != 0:
    quit(r)
argv.append('')
if argv[1] == "pg":
  # to use with KCacheGrind
  run(f"""
    g++ {EXAMPLE} {SRC_ARG} -o {EXAMPLE_BIN} {FLAGS} -g || exit 1
    valgrind --tool=callgrind --callgrind-out-file=callgrind.output {EXAMPLE_CMD} || exit 1
  """)
elif  argv[1] == "san":
  run(f"""
    g++ {EXAMPLE} {SRC_ARG} -o {EXAMPLE_BIN} {FLAGS} -g --sanitize=address || exit 1
    {EXAMPLE_CMD}
  """)
elif  argv[1] == "db":
  # simple gdb
  run(f"""
    g++ {EXAMPLE} {SRC_ARG} -o {EXAMPLE_BIN} {FLAGS} -g || exit 1
    gdb --args {EXAMPLE_CMD} || exit 1
  """)
elif  argv[1] == "clean":
  for l in listdir():
    if l.rsplit('.',1)[1] in ('out','a','so','o','output','vgcore'):
      print(f"rm {l}")
      remove(l)
elif  argv[1] == "shared":
  run(f"""
    g++ {SRC_ARG} -fPIC -shared -O3 -o bin/libbutch.so {FLAGS} || exit 1
    \\ls bin/libbutch.so
  """)
elif argv[1] == "static":
  for src, obj in zip(SRC, OBJ):
    run(f"g++ -c {src} -o {obj} -O3")
  run(f"ar rcs bin/libbutch.a {' '.join(OBJ)}")
  run(f"\\ls bin/libbutch.a", False)
else:
  run(f"g++ {EXAMPLE} {SRC_ARG} -o {EXAMPLE_BIN} -g {FLAGS} || exit 1")
  run(EXAMPLE_CMD)

