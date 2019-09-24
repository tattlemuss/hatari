#!/usr/bin/env python
#
# Copyright (C) 2012-2019 by Eero Tamminen <oak at helsinkinet fi>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
"""
Tester boots the given TOS versions under Hatari with all the possible
combinations of the given machine HW configuration options, that are
supported by the tested TOS version.

Verification screenshot is taken at the end of each boot before
proceeding to testing of the next combination.  Screenshot name
indicates the used combination, for example:
        etos512k-falcon-rgb-gemdos-14M.png
        etos512k-st-mono-floppy-1M.png


NOTE: If you want to test the latest, uninstalled version of Hatari,
you need to set PATH to point to your Hatari binary directory, like
this:
	PATH=../../build/src:$PATH tos_tester.py <TOS images>

If hconsole isn't installed to one of the standard locations (under
/usr or /usr/local), or you don't run this from within Hatari sources,
you also need to specify hconsole.py location with:
	export PYTHONPATH=/path/to/hconsole
"""

import getopt, os, signal, select, sys, time

def add_hconsole_paths():
    "add most likely hconsole locations to module import path"
    # prefer the devel version in Hatari sources, if it's found
    subdirs = len(os.path.abspath(os.curdir).split(os.path.sep))-1
    for level in range(subdirs):
        f = level*(".." + os.path.sep) + "tools/hconsole/hconsole.py"
        if os.path.isfile(f):
            f = os.path.dirname(f)
            sys.path.append(f)
            print("Added local hconsole path: %s" % f)
            break
    sys.path += ["/usr/local/share/hatari/hconsole",
                 "/usr/share/hatari/hconsole"]

add_hconsole_paths()
import hconsole


def warning(msg):
    "output warning message"
    sys.stderr.write("WARNING: %s\n" % msg)

def error_exit(msg):
    "output error and exit"
    sys.stderr.write("ERROR: %s\n" % msg)
    sys.exit(1)


# -----------------------------------------------
class TOS:
    "class for TOS image information"
    # objects have members:
    # - path (string),  given TOS image file path/name
    # - name (string),  filename with path and extension stripped
    # - size (int),     image file size, in kB
    # - etos (bool),    is EmuTOS?
    # - version (int),  TOS version
    # - memwait (int),  how many secs to wait before memcheck key press
    # - fullwait (int), after which time safe to conclude boot to have failed
    # - machines (tuple of strings), which Atari machines this TOS supports

    def __init__(self, path):
        self.path, self.size, self.name = self._add_file(path)
        self.version, self.etos = self._add_version()
        self.memwait, self.fullwait, self.machines = self._add_info()


    def _add_file(self, img):
        "get TOS file size and basename for 'img'"
        if not os.path.isfile(img):
            raise AssertionError("'%s' given as TOS image isn't a file" % img)

        size = os.stat(img).st_size
        tossizes = (196608, 262144, 524288)
        if size not in tossizes:
            raise AssertionError("image '%s' size not one of TOS sizes %s" % (img, repr(tossizes)))

        name = os.path.basename(img)
        name = name[:name.rfind('.')]
        return (img, size/1024, name)


    def _add_version(self):
        "get TOS version and whether it's EmuTOS & supports GEMDOS HD"
        f = open(self.path, 'rb')
        f.seek(0x2, 0)
        version = (ord(f.read(1)) << 8) + ord(f.read(1))
        # older TOS versions don't support autostarting
        # programs from GEMDOS HD dir with *.INF files
        f.seek(0x2C, 0)
        etos = (f.read(4) == "ETOS")
        return (version, etos)


    def _add_info(self):
        "add TOS version specific info of supported machines etc"
        name, size, version = self.name, self.size, self.version

        if self.etos:
            # EmuTOS 512k, 256k and 192k versions have different machine support
            if size == 512:
                # startup screen on falcon 14MB is really slow
                info = (5, 10, ("st", "megast", "ste", "megaste", "tt", "falcon"))
            elif size == 256:
                info = (2, 8, ("st", "megast", "ste", "megaste"))
            elif size == 192:
                info = (0, 6, ("st", "megast"))
            else:
                raise AssertionError("'%s' image size %dkB isn't valid for EmuTOS" % (name, size))
        # https://en.wikipedia.org/wiki/Atari_TOS
        elif version <= 0x100:
            # boots up really slow with 4MB
            info = (0, 16, ("st",))
        elif version <= 0x104:
            info = (0, 6, ("st", "megast"))
        elif version < 0x200:
            info = (0, 6, ("ste",))
        elif version < 0x300:
            # TOS v2.x are slower with VDI mode than others
            info = (2, 8, ("st", "megast", "ste", "megaste"))
        elif version < 0x400:
            # memcheck comes up fast, but boot takes time
            info = (2, 8, ("tt",))
        elif version < 0x500:
            # memcheck takes long to come up with 14MB
            info = (3, 8, ("falcon",))
        else:
            raise AssertionError("'%s' TOS version 0x%x isn't valid" % (name, version))

        if self.etos:
            print("%s is EmuTOS v%x %dkB" % (name, version, size))
        else:
            print("%s is normal TOS v%x" % (name, version))
        # 0: whether / how long to wait to dismiss memory test
        # 1: how long to wait until concluding test failed
        # 2: list of machines supported by this TOS version
        return info

    def supports_gemdos_hd(self):
        "whether TOS version supports Hatari's GEMDOS HD emulation"
        return self.version >= 0x0104

    def supports_hdinterface(self, hdinterface):
        "whether TOS version supports monitor that is valid for given machine"
        # EmuTOS doesn't require drivers to access DOS formatted disks
        if self.etos:
            # IDE support is in EmuTOS since 0.9.0, SCSI since 0.9.10
            if hdinterface != "acsi" and self.size < 512:
                return False
            return True
        # As ACSI (big endian) and IDE (little endian) images would require
        # diffent binary drivers on them and it's not possible to generate
        # such images automatically, testing ACSI & IDE images for normal
        # TOS isn't support.
        #
        # (And even with a driver, only TOS 4.x supports IDE.)
        return False

    def supports_monitor(self, monitortype, machine):
        "whether TOS version supports monitor that is valid for given machine"
        # other monitor types valid for the machine are
        # valid also for TOS that works on it
        if monitortype.startswith("vdi"):
            # VDI mode doesn't work properly until TOS v1.02
            if self.version < 0x102:
                return False
            # sensible sized VDI modes don't work with TOS4
            # (nor make sense with its Videl expander support)
            if self.version >= 0x400:
                return False
            if self.etos:
                # smallest EmuTOS image doesn't have any Falcon support
                if machine == "falcon" and self.size == 192:
                    return False
            # 2-plane modes don't work properly with real TOS
            elif monitortype.endswith("2"):
                return False
        return True

    def supports_32bit_addressing(self):
        "whether TOS version supports 32-bit addressing"
        if self.etos or self.version >= 0x300:
            return True
        return False


# -----------------------------------------------
def validate(args, full):
    "return set of members not in the full set and given args"
    return (set(args).difference(full), args)

class Config:
    "Test configuration and validator class"
    # full set of possible options
    all_disks = ("floppy", "gemdos", "acsi", "ide", "scsi")
    all_graphics = ("mono", "rgb", "vga", "tv", "vdi1", "vdi2", "vdi4")
    all_machines = ("st", "megast", "ste", "megaste", "tt", "falcon")
    all_memsizes = (0, 1, 2, 4, 6, 8, 10, 12, 14)

    # defaults
    fast = False
    bools = []
    disks = ("floppy", "gemdos", "scsi")
    graphics = ("mono", "rgb", "vga", "vdi1", "vdi4")
    machines = ("st", "ste", "megaste", "tt", "falcon")
    memsizes = (0, 4, 14)
    ttrams = (0, 32)

    def __init__(self, argv):
        longopts = ["bool=", "disks=", "fast", "graphics=", "help", "machines=", "memsizes=", "ttrams="]
        try:
            opts, paths = getopt.gnu_getopt(argv[1:], "b:d:fg:hm:s:t:", longopts)
        except getopt.GetoptError as error:
            self.usage(error)
        self.handle_options(opts)
        self.images = self.check_images(paths)
        configs = (self.disks, self.graphics, self.machines, self.memsizes, self.ttrams, self.bools)
        print("Test configuration:\n\t%s %s %s RAM=%s TTRAM=%s bool=%s\n" % configs)


    def check_images(self, paths):
        "validate given TOS images"
        images = []
        for img in paths:
            try:
                images.append(TOS(img))
            except AssertionError as msg:
                self.usage(msg)
        if len(images) < 1:
            self.usage("no TOS image files given")
        return images


    def handle_options(self, opts):
        "parse command line options"
        unknown = None
        for opt, arg in opts:
            args = arg.split(",")
            if opt in ("-h", "--help"):
                self.usage()
            if opt in ("-f", "--fast"):
                self.fast = True
            elif opt in ("-b", "--bool"):
                self.bools += args
            elif opt in ("-d", "--disks"):
                unknown, self.disks = validate(args, self.all_disks)
            elif opt in ("-g", "--graphics"):
                unknown, self.graphics = validate(args, self.all_graphics)
            elif opt in ("-m", "--machines"):
                unknown, self.machines = validate(args, self.all_machines)
            elif opt in ("-s", "--memsizes"):
                try:
                    args = [int(i) for i in args]
                except ValueError:
                    self.usage("non-numeric memory sizes: %s" % arg)
                unknown, self.memsizes = validate(args, self.all_memsizes)
            elif opt in ("-t", "--ttrams"):
                try:
                    args = [int(i) for i in args]
                except ValueError:
                    self.usage("non-numeric TT-RAM sizes: %s" % arg)
                for ram in args:
                    if ram < 0 or ram > 512:
                        self.usage("invalid TT-RAM (0-512) size: %d" % ram)
                self.ttrams = args
            if unknown:
                self.usage("%s are invalid values for %s" % (list(unknown), opt))


    def usage(self, msg=None):
        "output program usage information"
        name = os.path.basename(sys.argv[0])
        print(__doc__)
        print("""
Usage: %s [options] <TOS image files>

Options:
\t-h, --help\tthis help
\t-f, --fast\tspeed up boot with less accurate emulation:
\t\t\t"--fast-forward yes --fast-boot yes --fastfdc yes --timer-d yes"
\t-d, --disks\t%s
\t-g, --graphics\t%s
\t-m, --machines\t%s
\t-s, --memsizes\t%s
\t-t, --ttrams\t%s
\t-b, --bool\t(extra boolean Hatari options to test)

Multiple values for an option need to be comma separated. If some
option isn't given, default list of values will be used for that.

For example:
  %s \\
\t--disks gemdos \\
\t--machines st,tt \\
\t--memsizes 0,4,14 \\
\t--ttrams 0,32 \\
\t--graphics mono,rgb \\
\t--bool --compatible,--mmu
""" % (name, self.all_disks, self.all_graphics, self.all_machines, self.all_memsizes, self.ttrams, name))
        if msg:
            print("ERROR: %s\n" % msg)
        sys.exit(1)


    def valid_disktype(self, machine, tos, disktype):
        "return whether given disk type is valid for given machine / TOS version"
        if disktype == "floppy":
            return True
        if disktype == "gemdos":
            return tos.supports_gemdos_hd()

        if machine in ("st", "megast", "ste"):
            hdinterface = ("acsi",)
        elif machine in ("megaste", "tt"):
            hdinterface = ("acsi", "scsi")
        elif machine == "falcon":
            hdinterface = ("ide", "scsi")
        else:
            raise AssertionError("unknown machine %s" % machine)

        if disktype in hdinterface:
            return tos.supports_hdinterface(disktype)
        return False

    def valid_monitortype(self, machine, tos, monitortype):
        "return whether given monitor type is valid for given machine / TOS version"
        if machine in ("st", "megast", "ste", "megaste"):
            monitors = ("mono", "rgb", "tv", "vdi1", "vdi2", "vdi4")
        elif machine == "tt":
            monitors = ("mono", "vga", "vdi1", "vdi2", "vdi4")
        elif machine == "falcon":
            monitors = ("mono", "rgb", "vga", "vdi1", "vdi2", "vdi4")
        else:
            raise AssertionError("unknown machine %s" % machine)
        if monitortype in monitors:
            return tos.supports_monitor(monitortype, machine)
        return False

    def valid_memsize(self, machine, memsize):
        "return whether given memory size is valid for given machine"
        if machine in ("st", "megast", "ste", "megaste"):
            sizes = (0, 1, 2, 4)
        elif machine in ("tt", "falcon"):
            # 0 (512kB) isn't valid memory size for Falcon/TT
            sizes = self.all_memsizes[1:]
        else:
            raise AssertionError("unknown machine %s" % machine)
        if memsize in sizes:
            return True
        return False

    def valid_ttram(self, machine, tos, ttram, winuae):
        "return whether given TT-RAM size is valid for given machine"
        if machine in ("st", "megast", "ste", "megaste"):
            if ttram == 0:
                return True
        elif machine in ("tt", "falcon"):
            if ttram == 0:
                return True
            if not winuae:
                warning("TT-RAM / 32-bit addressing is supported only by Hatari WinUAE CPU core version")
                return False
            if ttram < 0 or ttram > 512:
                return False
            return tos.supports_32bit_addressing()
        else:
            raise AssertionError("unknown machine %s" % machine)
        return False

    def validate_bools(self, winuae):
        "exit with error if given bool option is invalid"
        # Several bool options are left out of these lists, either because
        # they can be problematic for running of the tests themselves, or
        # they should have no impact on emulation, only emulator.
        #
        # Below ones should be potentially relevant ones to test
        # (EmuTOS supports NatFeats so it can have impact too)
        generic_opts = ("--compatible", "--timer-d", "--fastfdc", "--fast-boot", "--natfeats")
        winuae_opts = ("--cpu-exact", "--mmu", "--addr24", "--fpu-softfloat")
        for option in self.bools:
            if option not in generic_opts:
                if option not in winuae_opts:
                    if winuae:
                        error_exit("bool option '%s' not in relevant options sets:\n\t%s\n\t%s" % (option, generic_opts, winuae_opts))
                    else:
                        error_exit("bool option '%s' not in relevant options set:\n\t%s" % (option, generic_opts))
                elif not winuae:
                    error_exit("bool option '%s' is supported only by WinUAE CPU core" % option)

    def valid_bool(self, machine, option):
        "return True if given bool option is relevant to test"
        if option in ("--mmu", "--addr24") and machine not in ("tt", "falcon"):
            # MMU & 32-bit addressing are relevant only for 030
            return False
        return True

# -----------------------------------------------
def verify_file_match(srcfile, dstfile):
    "return error string if sizes of given files don't match"
    if not os.path.exists(dstfile):
        return "file '%s' missing" % dstfile
    dstsize = os.stat(dstfile).st_size
    srcsize = os.stat(srcfile).st_size
    if dstsize != srcsize:
        return "file '%s' size %d doesn't match file '%s' size %d" % (srcfile, srcsize, dstfile, dstsize)

def verify_file_empty(filename):
    "return error string if given file isn't empty"
    if not os.path.exists(filename):
        return "file '%s' missing" % filename
    size = os.stat(filename).st_size
    if size != 0:
        return "file '%s' isn't empty (%d bytes)" % (filename, size)

class Tester:
    "test driver class"
    output = "output" + os.path.sep
    report = output + "report.txt"
    # dummy Hatari config file to force suitable default options
    dummycfg  = "dummy.cfg"
    defaults  = [sys.argv[0], "--configfile", dummycfg]
    testprg   = "disk" + os.path.sep + "GEMDOS.PRG"
    textinput = "disk" + os.path.sep + "TEXT"
    textoutput= "disk" + os.path.sep + "TEST"
    printout  = output + "printer-out"
    serialout = output + "serial-out"
    fifofile  = output + "midi-out"
    bootauto  = "bootauto.st.gz" # TOS old not to support GEMDOS HD either
    bootdesk  = "bootdesk.st.gz"
    floppyprg = "A:\MINIMAL.PRG"
    hdimage   = "hd.img"
    ideimage  = "hd.img"	 # for now use the same image as for ACSI
    hdprg     = "C:\MINIMAL.PRG"
    results   = None

    def __init__(self):
        "test setup initialization"
        self.cleanup_all_files()
        self.create_config()
        self.create_files()
        signal.signal(signal.SIGALRM, self.alarm_handler)
        hatari = hconsole.Hatari(["--confirm-quit", "no"])
        self.winuae = hatari.winuae
        hatari.kill_hatari()

    def alarm_handler(self, signum, dummy):
        "output error if (timer) signal came before passing current test stage"
        if signum == signal.SIGALRM:
            print("ERROR: timeout triggered -> test FAILED")
        else:
            print("ERROR: unknown signal %d received" % signum)
            raise AssertionError

    def create_config(self):
        "create Hatari configuration file for testing"
        # write specific configuration to:
        # - avoid user's own config
        # - get rid of the dialogs
        # - don't warp mouse on resolution changes
        # - limit Videl zooming to same sizes as ST screen zooming
        # - get rid of statusbar and borders in TOS screenshots
        #   to make them smaller & more consistent
        # - disable GEMDOS emu by default
        # - use empty floppy disk image to avoid TOS error when no disks
        # - set printer output file
        # - disable serial in and set serial output file
        # - disable MIDI in, use MIDI out as fifo file to signify test completion
        dummy = open(self.dummycfg, "w")
        dummy.write("[Log]\nnAlertDlgLogLevel = 0\nbConfirmQuit = FALSE\n\n")
        dummy.write("[Screen]\nnMaxWidth = 832\nnMaxHeight = 576\nbCrop = TRUE\nbAllowOverscan = FALSE\nbMouseWarp = FALSE\n\n")
        dummy.write("[HardDisk]\nbUseHardDiskDirectory = FALSE\n\n")
        dummy.write("[Floppy]\nszDiskAFileName = blank-a.st.gz\n\n")
        dummy.write("[Printer]\nbEnablePrinting = TRUE\nszPrintToFileName = %s\n\n" % self.printout)
        dummy.write("[RS232]\nbEnableRS232 = TRUE\nszInFileName = \nszOutFileName = %s\n" % self.serialout)
        dummy.write("bEnableSccB = TRUE\nsSccBOutFileName = %s\n\n" % self.serialout)
        dummy.write("[Midi]\nbEnableMidi = TRUE\nsMidiInFileName = \nsMidiOutFileName = %s\n\n" % self.fifofile)
        dummy.close()

    def cleanup_all_files(self):
        "clean out any files left over from last run"
        for path in (self.fifofile, "grab0001.png", "grab0001.bmp"):
            if os.path.exists(path):
                os.remove(path)
        self.cleanup_test_files()

    def create_files(self):
        "create files needed during testing"
        if not os.path.exists(self.output):
            os.mkdir(self.output)
        if not os.path.exists(self.fifofile):
            os.mkfifo(self.fifofile)

    def get_screenshot(self, instance, identity):
        "save screenshot of test end result"
        instance.run("screenshot")
        if os.path.isfile("grab0001.png"):
            os.rename("grab0001.png", self.output + identity + ".png")
        elif os.path.isfile("grab0001.bmp"):
            os.rename("grab0001.bmp", self.output + identity + ".bmp")
        else:
            warning("failed to locate screenshot grab0001.{png,bmp}")

    def cleanup_test_files(self):
        "remove unnecessary files at end of test"
        for path in (self.serialout, self.printout):
            if os.path.exists(path):
                os.remove(path)

    def verify_output(self, identity, tos, memory):
        "do verification on all test output"
        # both tos version and amount of memory affect what
        # GEMDOS operations work properly...
        ok = True
        # check file truncate
        error = verify_file_empty(self.textoutput)
        if error:
            print("ERROR: file wasn't truncated:\n\t%s" % error)
            os.rename(self.textoutput, "%s.%s" % (self.textoutput, identity))
            ok = False
        # check serial output
        error = verify_file_match(self.textinput, self.serialout)
        if error:
            print("ERROR: serial output doesn't match input:\n\t%s" % error)
            os.rename(self.serialout, "%s.%s" % (self.serialout, identity))
            ok = False
        # check printer output
        error = verify_file_match(self.textinput, self.printout)
        if error:
            prname = "%s.%s" % (self.printout, identity)
            if tos.etos or tos.version > 0x206 or (tos.version == 0x100 and memory > 1):
                print("ERROR: printer output doesn't match input (EmuTOS, TOS v1.00 or >v2.06)\n\t%s" % error)
                if os.path.exists(self.printout):
                    os.rename(self.printout, prname)
                else:
                    open(prname, 'w')
                ok = False
            else:
                if os.path.exists(self.printout):
                    error = verify_file_empty(self.printout)
                    if error:
                        print("WARNING: unexpected printer output (TOS v1.02 - TOS v2.06):\n\t%s" % error)
                        os.rename(self.printout, prname)
        self.cleanup_test_files()
        return ok


    def wait_fifo(self, fifo, timeout):
        "wait_fifo(fifo) -> wait until fifo has input until given timeout"
        print("Waiting %ss for FIFO '%s' input..." % (timeout, self.fifofile))
        sets = select.select([fifo], [], [], timeout)
        if sets[0]:
            print("...test program is READY, read its FIFO for test-case results:")
            try:
                # read can block, make sure it's eventually interrupted
                signal.alarm(timeout)
                line = fifo.readline().strip()
                signal.alarm(0)
                print("=> %s" % line)
                return (True, (line == "success"))
            except IOError:
                pass
        print("ERROR: TIMEOUT without FIFO input, BOOT FAILED")
        return (False, False)


    def open_fifo(self, timeout):
        "open FIFO for test program output"
        try:
            signal.alarm(timeout)
            # open returns after Hatari has opened the other
            # end of fifo, or when SIGALARM interrupts it
            fifo = open(self.fifofile, "r")
            # cancel signal
            signal.alarm(0)
            return fifo
        except IOError:
            print("ERROR: FIFO open IOError!")
            return None

    def test(self, identity, testargs, tos, memory, extrawait):
        "run single boot test with given args and waits"
        # Hatari command line options, don't exit if Hatari exits
        instance = hconsole.Main(self.defaults + testargs, False)
        fifo = self.open_fifo(tos.fullwait)
        if not fifo:
            print("ERROR: failed to get FIFO to Hatari!")
            self.get_screenshot(instance, identity)
            instance.run("kill")
            return (False, False, False, False)
        else:
            init_ok = True

        if tos.memwait:
            # pass memory test
            time.sleep(tos.memwait)
            instance.run("keypress %s" % hconsole.Scancode.Space)

        # wait until test program has been run and output something to fifo
        prog_ok, tests_ok = self.wait_fifo(fifo, tos.fullwait + extrawait)
        if tests_ok:
            output_ok = self.verify_output(identity, tos, memory)
        else:
            output_ok = False

        # get screenshot after a small wait (to guarantee all
        # test program output got to screen even with frameskip)
        time.sleep(0.2)
        self.get_screenshot(instance, identity)
        # get rid of this Hatari instance
        instance.run("kill")
        return (init_ok, prog_ok, tests_ok, output_ok)


    def prepare_test(self, config, tos, machine, monitor, disk, memory, ttram, extra):
        "compose test ID and Hatari command line args, then call .test()"
        identity = "%s-%s-%s-%s-%dM-%dM" % (tos.name, machine, monitor, disk, memory, ttram)
        testargs = ["--tos", tos.path, "--machine", machine, "--memsize", str(memory)]
        if self.winuae:
            if ttram:
                testargs += ["--addr24", "off", "--ttram", str(ttram)]
            else:
                testargs += ["--addr24", "on"]

        if extra:
            identity += "-%s%s" % (extra[0].replace("-", ""), extra[1])
            testargs += extra

        if monitor.startswith("vdi"):
            planes = monitor[-1]
            testargs += ["--vdi-planes", planes]
            if planes == "1":
                testargs += ["--vdi-width", "800", "--vdi-height", "600"]
            elif planes == "2":
                testargs += ["--vdi-width", "640", "--vdi-height", "480"]
            else:
                testargs += ["--vdi-width", "640", "--vdi-height", "400"]
        else:
            testargs += ["--monitor", monitor]

        if config.fast:
            testargs += ["--fast-forward", "yes", "--fast-boot", "yes",
                         "--fastfdc", "yes", "--timer-d", "yes"]

        extrawait = 0
        if disk == "gemdos":
            # use Hatari autostart, must be last thing added to testargs!
            testargs += [self.testprg]
        # HD supporting TOSes support also INF file autostart, so
        # with them test program can be run with the supplied INF
        # file.
        #
        # However, in case of VDI resolutions the VDI resolution
        # setting requires overriding the INF file...
        #
        # -> always specify directly which program to autostart
        #    with --auto
        elif disk == "floppy":
            if tos.supports_gemdos_hd():
                testargs += ["--disk-a", self.bootdesk, "--auto", self.floppyprg]
            else:
                testargs += ["--disk-a", self.bootauto]
            # floppies are slower
            extrawait = 3
        elif disk == "acsi":
            testargs += ["--acsi", "0=%s" % self.hdimage, "--auto", self.hdprg]
        elif disk == "scsi":
            testargs += ["--scsi", "0=%s" % self.hdimage, "--auto", self.hdprg]
        elif disk == "ide":
            testargs += ["--ide-master", self.ideimage, "--auto", self.hdprg]
        else:
            raise AssertionError("unknown disk type '%s'" % disk)

        results = self.test(identity, testargs, tos, memory, extrawait)
        self.results[tos.name].append((identity, results))

    def run(self, config):
        "run all TOS boot test combinations"
        config.validate_bools(self.winuae)

        self.results = {}
        for tos in config.images:
            self.results[tos.name] = []
            print("\n***** TESTING: %s *****\n" % tos.name)
            count = 0
            for machine in config.machines:
                if machine not in tos.machines:
                    continue
                for monitor in config.graphics:
                    if not config.valid_monitortype(machine, tos, monitor):
                        continue
                    for memory in config.memsizes:
                        if not config.valid_memsize(machine, memory):
                            continue
                        for ttram in config.ttrams:
                            if not config.valid_ttram(machine, tos, ttram, self.winuae):
                                continue
                            for disk in config.disks:
                                if not config.valid_disktype(machine, tos, disk):
                                    continue
                                if config.bools:
                                    for opt in config.bools:
                                        if not config.valid_bool(machine, opt):
                                            continue
                                        for val in ('on', 'off'):
                                            self.prepare_test(config, tos, machine, monitor, disk, memory, ttram, [opt, val])
                                            count += 1
                                else:
                                    self.prepare_test(config, tos, machine, monitor, disk, memory, ttram, None)
                                    count += 1
            if not count:
                warning("no matching configuration for TOS '%s'" % tos.name)
        self.cleanup_all_files()

    def summary(self):
        "summarize test results"
        cases = [0, 0, 0, 0]
        passed = [0, 0, 0, 0]
        tosnames = list(self.results.keys())
        tosnames.sort()

        report = open(self.report, "w")
        report.write("\nTest report:\n------------\n")
        for tos in tosnames:
            configs = self.results[tos]
            if not configs:
                report.write("\n+ WARNING: no configurations for '%s' TOS!\n" % tos)
                continue
            report.write("\n+ %s:\n" % tos)
            for config, results in configs:
                # convert True/False bools to FAIL/pass strings
                values = [("FAIL", "pass")[int(r)] for r in results]
                report.write("  - %s: %s\n" % (config, values))
                # update statistics
                for idx in range(len(results)):
                    cases[idx] += 1
                    passed[idx] += results[idx]

        report.write("\nSummary of FAIL/pass values:\n")
        idx = 0
        for line in ("Hatari init", "Test program running", "Test program test-cases", "Test program output"):
            passes, total = passed[idx], cases[idx]
            if passes < total:
                if not passes:
                    result = "all %d FAILED" % total
                else:
                    result = "%d/%d passed" % (passes, total)
            else:
                result = "all %d passed" % total
            report.write("- %s: %s\n" % (line, result))
            idx += 1
        report.write("\n")

        # print report out too
        print("--- %s ---" % self.report)
        report = open(self.report, "r")
        for line in report.readlines():
            print(line.strip())


# -----------------------------------------------
def main():
    "tester main function"
    info = "Hatari TOS bootup tester"
    print("\n%s\n%s\n" % (info, "-"*len(info)))
    config = Config(sys.argv)
    tester = Tester()
    tester.run(config)
    tester.summary()

if __name__ == "__main__":
    main()
