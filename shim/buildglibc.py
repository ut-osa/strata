#!/usr/bin/python


import sys, os, string, subprocess, shutil, fileinput, multiprocessing, re, resource


def replaceAll(fd,searchExp,replaceExp):
    for line in fileinput.input(fd, inplace=1):
        if searchExp in line:
            line = line.replace(searchExp,replaceExp)
        sys.stdout.write(line)

def prependText(filename, text) :
    data = ""
    with open(filename, 'r') as original:
        data = original.read()
    with open(filename, 'w') as modified:
        modified.write(text)
        modified.write(data)

def appendText(filename, text) :
    with open(filename, "a") as myfile:
        myfile.write(text)


home = os.getcwd()
glibc = "glibc-2.19"
glibcParent = "" # glibc parent directory
glibcDir = ""    # glibc dir (ex. glibc-2.19)
# (iangneal): change this to make distribution of prebuilt binaries easier.
#buildDir = "glibc-build"
#installDir = os.path.dirname(home) + '/Runtime/'
buildDir = "glibc-build-all"
installDir = os.path.abspath(".") + "/glibc-build"
do_install = False
commandStr = ""
commandOutput = ""
quiet = False
debug_flags = ""

for arg in sys.argv[1:]:
    if arg == '--quiet' or arg == '-q':
        quiet = True
    if arg == '--debug':
        debug_flags = "-g"
    if arg == 'install':
        do_install = True

    #########################################
    #### get the locations of directories ###
    #########################################

if not do_install:

    if not quiet:
        iput = raw_input('use {0} as the source of GNU libc? ([y]/n):'.format(glibc)).lower()
        if not iput == 'y' and not iput == '' :
            glibc = raw_input('enter the glibc source to install with: ')

    if not quiet:
        iput = raw_input('{0} contains glibc code to compile? ([y]/n): '.format(glibc)).lower()
        if not iput == 'y' and not iput == '':
            glibc = raw_input('directory containing glibc code to compile: ')

    if os.path.isdir(glibc) :
        glibc = os.path.abspath(glibc)
        glibcParent,glibcDir = os.path.split(glibc)
        print 'building in {0}: {1}'.format(glibcParent, glibcDir)

    if not quiet:
        iput = raw_input('use {0} as the directory to build glibc in? ([y]/n): '.format(buildDir)).lower()
        if not iput == 'y' and not iput == '':
            buildDir = raw_input('the directory to build glibc in:  ')

    buildDir = os.path.abspath(buildDir)
    print 'using build dir: {0}'.format(buildDir)
    if os.path.isdir(buildDir) :
        if not quiet:
            clean = raw_input('clean build (delete {0}, rerun configure, etc.)? ([y]/n): '.format(buildDir))
        else:
            clean = 'y'

        if clean == 'y' or clean == '':
            shutil.rmtree(buildDir)
            os.makedirs(buildDir)
        else :
            print 'Then just go to {0} and type make...'.format(buildDir)
            exit(0)
    else :
        os.makedirs(buildDir)

if do_install and not quiet:

    iput = raw_input('use {0} as the directory to install glibc in? ([y]/n): '.format(installDir)).lower()
    if not iput == 'y' and not iput == '':
        installDir = raw_input('the directory to install glibc in:  ')

    installDir = os.path.abspath(installDir)
    print 'using install dir: {0}'.format(installDir)



    ################################
    #### doctor glibc's Makefile ###
    ################################

if not do_install:

    os.chdir(buildDir)

    if len(debug_flags) != 0:
        print "Debug build"
        cflags = '-g -Og -U_FORTIFY_SOURCE -fno-stack-protector'
    else:
        cflags = '-O2 -U_FORTIFY_SOURCE -fno-stack-protector'

    extra_defs = ''
    disabled_features = { 'nscd' }
    extra_flags = '--with-tls --enable-add-ons=nptl --without-selinux --disable-test {0}'.format(' '.join(['--disable-' + f for f in disabled_features]))

    ##    configure
    commandStr = r'CFLAGS="{2}" {3} {0}/configure --prefix={1} {4} | tee configure.out'.format(glibc, installDir, cflags, extra_defs, extra_flags)
    print commandStr
    commandOutput = subprocess.call(commandStr, shell=True)

    ##    Enable parallel builds
    numCPUs = multiprocessing.cpu_count()
    ##    Don't use up all the cores!
    numCPUs = numCPUs - 1
    if numCPUs == 0:
        numCPUs = 1
    replaceAll('Makefile', r'# PARALLELMFLAGS = -j4', r'PARALLELMFLAGS = -j{0}'.format(numCPUs))


link_binaries     = [ ( 'elf',    'ld-linux-x86-64.so.2' ),
                      ( 'nptl',   'libpthread.so.0' ),
                      ( 'nptl_db','libthread_db.so.1' ),
                      ( 'math',   'libm.so.6' ),
                      ( 'dlfcn',  'libdl.so.2' ),
                      ( 'login',  'libutil.so.1' ),
                      ( 'csu',    'crt1.o' ),
                      ( 'csu',    'crti.o' ),
                      ( 'csu',    'crtn.o' ),
                      ( 'rt',     'librt.so.1' ) ]


if not do_install:

    for (dir, bin) in link_binaries:
        print bin + ' -> ' + dir + '/' + bin
        os.symlink(dir + '/' + bin, bin)

    print '\n\n\nNow type \'make\' in \'{0}\'\n\n'.format(buildDir)



if do_install:
    install_binaries  = [ ( 'elf',    'ld-linux-x86-64.so.2' ),
                          ( 'nptl',   'libpthread.so.0' ),
                          ( 'nptl_db','libthread_db.so.1' ),
                          ( 'math',   'libm.so.6' ),
                          ( '.',      'libc.so' ),
                          ( 'dlfcn',  'libdl.so.2' ),
                          ( 'login',  'libutil.so.1' ),
                          ( 'csu',    'crt1.o' ),
                          ( 'csu',    'crti.o' ),
                          ( 'csu',    'crtn.o' ),
                          ( 'rt',     'librt.so.1' ) ]

    link_binaries = [ ( 'libc.so', 'libc.so.6' ) ]

    for (dir, bin) in install_binaries:
        print installDir + '/' + bin + ' -> ' + buildDir + '/' + dir + '/' + bin
        if not os.path.lexists(installDir + '/' + bin):
            shutil.copyfile(buildDir + '/' + dir + '/' + bin, installDir + '/' + bin)
            os.chmod(installDir + '/' + bin, 0777)

    for (src, bin) in link_binaries:
        print installDir + '/' + bin + ' -> ' + buildDir + '/' + src
        if not os.path.lexists(installDir + '/' + bin):
            os.symlink(src, installDir + '/' + bin)
