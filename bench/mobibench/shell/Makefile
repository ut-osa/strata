
#CROSS=arm-none-linux-gnueabi-
#CROSS=

EXENAME = mobibench

SRCS = mobibench.c sqlite3.c

INSTALL = install

prefix = /usr/local
bindir = $(prefix)/bin

CFLAGS = -lpthread -ldl -g

# Strata FS 
STRATA = ../../..
STRATA_INCS = $(STRATA)/libfs/src/
STRATA_INCS += $(STRATA)/libfs/lib/nvml/src/include/
STRATA_INC = $(addprefix -I, $(STRATA_INCS))

STRATA_LIBS = $(STRATA)/libfs/build/
STRATA_LIBS += $(STRATA)/libfs/src/storage/spdk/
STRATA_LIBS += $(STRATA)/libfs/lib/nvml/src/nondebug/
STRATA_LIB = $(addprefix -L, $(STRATA_LIBS))
STRATA_FLAGS = $(STRATA_INC) $(STRATA_LIB) -lmlfs -lspdk -lpmem


#CFLAGS += -DDEBUG_SCRIPT

#for sqltie3
CFLAGS += -DNDEBUG=1 \
     -DHAVE_USLEEP=1 \
     -DSQLITE_HAVE_ISNAN \
     -DSQLITE_DEFAULT_JOURNAL_SIZE_LIMIT=1048576 \
     -DSQLITE_THREADSAFE=2 \
     -DSQLITE_TEMP_STORE=3 \
     -DSQLITE_POWERSAFE_OVERWRITE=1 \
     -DSQLITE_DEFAULT_FILE_FORMAT=4 \
     -DSQLITE_DEFAULT_AUTOVACUUM=1 \
     -DSQLITE_ENABLE_MEMORY_MANAGEMENT=1 \
     -DSQLITE_ENABLE_FTS3 \
     -DSQLITE_ENABLE_FTS3_BACKWARDS \
     -DSQLITE_ENABLE_FTS4 \
     -DSQLITE_OMIT_BUILTIN_TEST \
     -DSQLITE_OMIT_COMPILEOPTION_DIAGS \
     -DSQLITE_OMIT_LOAD_EXTENSION \
     -DSQLITE_DEFAULT_FILE_PERMISSIONS=0666
     #-DUSE_PREAD64: not supported in strata
     #--static

all :
	$(CROSS)gcc -o $(EXENAME) $(SRCS) $(CFLAGS) $(STRATA_FLAGS)

clean :
	@rm -rf mobibench

install : $(EXENAME)
	@$(INSTALL) -m 755 -d $(DESTDIR)$(bindir)
	@$(INSTALL) $(EXENAME) $(DESTDIR)$(bindir)
