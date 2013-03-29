SUBDIRS = lib src test
DISTONLY_SUBDIRS = bindings doc util vcc
pkginclude_HEADERS = senna.h
bin_SCRIPTS = senna-cfg

LCOV_TMP = lcov_tmp
LCOV_HTML = lcov_html
LCOV_LCOV = @LCOV@
LCOV_GENHTML = @GENHTML@

dist-hook:
# remove vcc
	rm -rf $(distdir)/vcc
# copy dist-only files
	for subdir in $(DISTONLY_SUBDIRS); do \
		cp -rp $$subdir $(distdir)/$$subdir; \
	done
# for mysql udf
	cd $(distdir)/bindings/mysql/udf/; ./autogen.sh; ./configure ; make distclean
	rm -rf `find $(distdir) -name autogen.sh`
# for remove svn directories
	rm -rf `find $(distdir) -name .svn`
# misc
	rm -rf `find $(distdir) -name '*.loT'`
	rm -rf `find $(distdir) -name '*.cache'`
	rm -rf `find $(distdir) -name '*.bak'`
	rm -rf `find $(distdir) -name '*.orig'`

ruby-bindings:
	cd bindings/ruby && \
	ruby extconf.rb && \
	make clean && make

install-ruby-bindings:
	cd bindings/ruby && \
	make install

test-ruby-bindings:
	cd bindings/ruby/test && \
	ruby all_test.rbx

valgrind-ruby-bindings:
	cd bindings/ruby/test && \
	valgrind --leak-check=full --show-reachable=yes ruby all_test.rbx

lcov-ruby-bindings: test-ruby-bindings lcov

if GCOV_ENABLED
lcov:
	rm -rf $(LCOV_TMP)
	$(mkinstalldirs) $(LCOV_TMP)
	cd $(LCOV_TMP) && \
	find ../lib -name \*.c -type f -exec cp -p {} . \; && \
	find ../lib -name \*.gcda -type f | while read fn; do \
		cp -p $$fn .; \
		cp -p $${fn%.gcda}.gcno .; \
	done
	$(LCOV_LCOV) -c -f -d $(LCOV_TMP) -o $(LCOV_TMP)/lcov.info
	$(mkinstalldirs) $(LCOV_HTML)
	$(LCOV_GENHTML) --prefix . --output-directory $(LCOV_HTML) --title "Senna Code Coverage" $(LCOV_TMP)/lcov.info
	@echo ""
	@echo "Show $(LCOV_HTML)/index.html to see the code coverage."
	@echo ""
else
.PHONY: lcov
lcov:
	@echo "You must specify --enable-gcov at configure time"
endif
