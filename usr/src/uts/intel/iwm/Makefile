#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2026 lex0de <lex0de@tuta.com>
#

UTSBASE		= ../..
MODULE		= iwm
OBJECTS		= $(IWM_OBJS:%=$(OBJS_DIR)/%)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

include $(UTSBASE)/intel/Makefile.intel

ALL_TARGET	= $(BINARY)
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

# Only mac_init_ops/mac_fini_ops are called.  firmload is part of genunix.
# Add net80211 when the driver actually registers with the wireless stack.
LDFLAGS		+= -Nmisc/mac

.KEEP_STATE:

def:		$(DEF_DEPS)
all:		$(ALL_DEPS)
clean:		$(CLEAN_DEPS)
clobber:	$(CLOBBER_DEPS)
install:	$(INSTALL_DEPS)

check:
	$(CSTYLE) -pP $(UTSBASE)/common/io/iwm/if_iwm.c
	$(CSTYLE) -pP $(UTSBASE)/common/io/iwm/if_iwmreg.h
	$(CSTYLE) -pP $(UTSBASE)/common/io/iwm/if_iwmvar.h
	$(HDRCHK) $(UTSBASE)/common/io/iwm/if_iwmreg.h
	$(HDRCHK) $(UTSBASE)/common/io/iwm/if_iwmvar.h

include $(UTSBASE)/intel/Makefile.targ
