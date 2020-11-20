# xv6

### How to run
- `make qemu-nox` may compile and start the xv6 OS
- `make clean` will delete executables and generated OS image

### Declaration of revision on xv6 source code
Our changes on xv6 source code are mainly on following files:
- mkfs.c: add `bin` directory and put all user programs into during OS initialization
- exec.c: check `/bin` first before execution
- path_util.h: path related functions, including get current working directory, and path concatenation
- pwd.c: get absolute current working directory, and relative path within container
- sh.c: check whether path to switch is valid within container
- proc.[h.c]:
  + For the data structure of container and process, and initialization part(cinit(), userinit(), fork(), cfork()),we use [xv6c](https://github.com/kierangilliam/xv6c) and this [project_report](https://courses.cs.washington.edu/courses/cse481a/18wi/projects/payload.pdf) as a reference
  + For other parts like wait(), scheduler(), we do it on our own
- cont.c: for the interface design(stages of container, style of commands) we use [ctoos](https://github.com/kierangilliam/xv6c/blob/master/ctool.c) as a reference
- For syscall-related files, we mainly reference [this blog](https://medium.com/@viduniwickramarachchi/add-a-new-system-call-in-xv6-5486c2437573) on how to add syscall to xv6
- path_concatenation_test.c: a testcase file to test the functionality of path utils in path_util.h
- testcases.txt: a sequence of commands and what to expect as testcases

### Contribution
This project is a groupwork of Duke COMSCI510 Advanced Operating System.
group member:
- JunyiXiao(netID: jx94), github account: https://github.com/yolande0917
- Hao Jiang(netID: hj110), github account: https://github.com/dentiny