#!/usr/bin/env python3
import signal
import subprocess
import sys
from getopt import GetoptError, gnu_getopt

SHORT_OPTS = "hbir"
LONG_OPTS = [
    "help",
    "run",
    "init-disk",
    "install-boot",
    "build-lib",
]

OPT_HELP = "HELP"
OPT_INIT_DISK = "INIT_DISK"
OPT_INSTALL_BOOT = "INSTALL_BOOT"
OPT_BUILD_LIB = "BUILD_LIB"
OPT_BUILD_KERNEL = "BUILD_KERNEL"
OPT_INSTALL_KERNEL = "INSTALL_KERNEL"
OPT_RUN = "RUN"

SUBPROCESS: subprocess.Popen[str] | None = None


def help():
    print("Usage: bigos [option...]")
    print(f"  {'-h, --help':<30}Display this information")
    print(f"  {'-b':<30}build kernel, but do not install")
    print(f"  {'-i':<30}build and install kernel")
    print(f"  {'-r, --run':<30}run kernel in bochs")
    print(f"  {'--init-disk':<30}build and install mbr, dbr ...")
    print(f"  {'--install-boot':<30}build and install bootloader")
    print(f"  {'--build-lib':<30}build libs which kernel needs")
    return None


def sigintHandler(signal, frame):
    if SUBPROCESS is not None:
        SUBPROCESS.send_signal(signal)
    else:
        sys.exit(-2)


class Log:
    @staticmethod
    def info(obj: object):
        print(str(obj))
        return None

    @staticmethod
    def success(obj: object):
        print("\033[32m" + str(obj) + "\033[0m")
        return None

    @staticmethod
    def warning(obj: object):
        print("\033[93m" + str(obj) + "\033[0m")
        return None

    @staticmethod
    def error(obj: object):
        print("\033[31merror:" + str(obj) + "\033[0m")
        return None

    @staticmethod
    def fatal(obj: object, code: int = 1):
        print("\033[31merror:" + str(obj) + "\033[0m")
        exit(code)


def parseOpts(argv: list[str]) -> tuple[dict[str, bool], list[str]]:
    try:
        opts, args = gnu_getopt(argv, SHORT_OPTS, LONG_OPTS)
    except GetoptError as error:
        Log.error(error)
        return ({}, [])

    taskMap = {}
    for opt, _val in opts:
        if opt in ["-h", "--help"]:
            taskMap[OPT_HELP] = True
        elif opt in ["--init-disk"]:
            taskMap[OPT_INIT_DISK] = True
        elif opt in ["--install-boot"]:
            taskMap[OPT_INSTALL_BOOT] = True
        elif opt in ["--build-lib"]:
            taskMap[OPT_BUILD_LIB] = True
        elif opt in ["-b"]:
            taskMap[OPT_BUILD_KERNEL] = True
        elif opt in ["-i"]:
            taskMap[OPT_BUILD_KERNEL] = True
            taskMap[OPT_INSTALL_KERNEL] = True
        elif opt in ["-r"]:
            taskMap[OPT_RUN] = True

    return (taskMap, args)


def process(cmds):
    global SUBPROCESS
    SUBPROCESS = subprocess.Popen(
        cmds,
        shell=True,
        stdin=sys.stdin,
        stdout=sys.stdout,
        stderr=sys.stderr,
        close_fds=True,
        universal_newlines=True,
        bufsize=1,
    )
    SUBPROCESS.communicate()

    code = SUBPROCESS.returncode
    SUBPROCESS = None

    return code


def initDisk():
    return None


def installBoot():
    return None


def buildLibs():
    return None


def buildKernel():
    cmds = ["./t"]
    code = process(cmds)
    if code == 0:
        Log.success("build kernel successfully")
    else:
        Log.error("build kernel failed with return code: " + str(code))
    return None


def installKernel():
    Log.error("install kernel failed")
    return None


def run():
    cmds = ["make"]
    process(cmds)
    return None


def main(argv):
    taskMap, _args = parseOpts(argv)
    if taskMap.get(OPT_HELP, False):
        help()
    if taskMap.get(OPT_INIT_DISK, False):
        initDisk()
    if taskMap.get(OPT_INSTALL_BOOT, False):
        installBoot()
    if taskMap.get(OPT_BUILD_LIB, False):
        buildLibs()
    if taskMap.get(OPT_BUILD_KERNEL, False):
        buildKernel()
    if taskMap.get(OPT_INSTALL_KERNEL, False):
        installKernel()
    if taskMap.get(OPT_RUN, False):
        run()
    return None


if __name__ == "__main__":
    signal.signal(signal.SIGINT, sigintHandler)
    main(sys.argv[1:])
