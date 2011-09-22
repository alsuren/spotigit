TARGET=git-spot
LDLIBS += -lreadline -lpthread -lrt
CFLAGs += -Werror
CFLAGS += -ggdb3
CFLAGS += -O0

include ../common.mk

$(TARGET): git-spot.o git-spot-posix.o appkey.o cmd.o browse.o search.o toplist.o inbox.o star.o social.o save.o playlist.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $^ -o $@
ifdef DEBUG
ifeq ($(shell uname),Darwin)
	install_name_tool -change @loader_path/../Frameworks/libspotify.framework/libspotify @rpath/libspotify.so $@
endif
endif
