##
# New build wrappers.
# Use: "make V=1" to see full GCC output.
#
ifdef V
  ifeq ("$(origin V)", "command line")
    KBUILD_VERBOSE = $(V)
  endif
endif
ifndef KBUILD_VERBOSE
  KBUILD_VERBOSE = 0
endif
ifeq ($(KBUILD_VERBOSE),1)
Q            =
MXFLAGS      =
LIBTOOLFLAGS = 
REDIRECT     =
else
Q            = @
MXFLAGS      = --no-print-directory
LIBTOOLFLAGS = --silent
REDIRECT     = >/dev/null
endif
export Q MXFLAGS REDIRECT

##
# Auto dependency creation
# Put the following line in the beginning of your Makefile:
# include rules.mk
# And this at the end:
# ifneq ($(MAKECMDGOALS),clean)
# -include $(DEPS)
# endif
#
##
# Smart autodependecy generation via GCC -M.
.%.d: %.c
	$(Q)$(SHELL) -ec "$(CC) -MM $(CFLAGS) $(CPPFLAGS) $< 2>/dev/null \
		| sed 's,.*: ,$*.o $@ : ,g' > $@; \
                [ -s $@ ] || rm -f $@"

##
# Override default implicit rules
%.tex: %.c
ifdef Q
	@printf "  GDOC    $(subst $(ROOTDIR)/,,$(shell pwd))/$@\n"
endif
	$(Q)doc2 -latex $< > $@

%.o: %.y
ifdef Q
	@printf "  YACC    $(subst $(ROOTDIR)/,,$(shell pwd))/$@\n"
endif
	$(Q)$(YACC) $(YFLAGS) $< 
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ y.tab.c
	$(Q)$(RM) y.tab.c

%.o: %.c
ifdef Q
	@printf "  CC      $(subst $(ROOTDIR)/,,$(shell pwd))/$@\n"
endif
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%: %.o
ifdef Q
	@printf "  LINK    $(subst $(ROOTDIR)/,,$(shell pwd))/$@\n"
endif
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-Map,$@.map -o $@ $^ $(LDLIBS$(LDLIBS-$(@)))

(%.o): %.c
ifdef Q
	@printf "  AR      $(subst $(ROOTDIR)/,,$(shell pwd))/$(notdir $@)($%)\n"
endif
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $*.o
	$(Q)$(AR) $(ARFLAGS) $@ $*.o

