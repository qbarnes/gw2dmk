include defines.mk
include product.mk

build_dir	?= build

ifndef top_dir
top_dir		:= $(shell realpath \
			--relative-to '$(CURDIR)/$(build_dir)' '$(CURDIR)')
else
top_dir		:= $(shell realpath \
			--relative-to '$(CURDIR)/$(build_dir)' '$(top_dir)')
endif

src_dir		?= $(top_dir)/src
inc_dir		?= $(top_dir)/src

clean_files	 =
clobber_files	 = $(clean_files) $(build_dir)
distclean_files	 = $(clobber_files) build build.*

skip_goals	 = clean clobber distclean show_package show_version
build_make_goals = $(or $(filter-out $(skip_goals),$(MAKECMDGOALS)), all)

build_make = $(MAKE) \
		-C '$(build_dir)' \
		-I '$(top_dir)' \
		-f '$(top_dir)/Makefile.build' \
		'src_dir=$(src_dir)' \
		'inc_dir=$(inc_dir)'


$(build_make_goals): FORCE | $(build_dir)
	$(build_make) $(build_make_goals)

$(build_dir):
	mkdir -p -- '$@'

clean:
	[ ! -d '$(build_dir)' ] || $(build_make) '$@'

clobber distclean:
	$(call scrub_files_call,$($@_files))


show_package:
	@echo $(PRODUCT)

show_version:
	@echo $(VERSION)


.PHONY: FORCE all
.PHONY: clean clobber distclean show_package show_version
.DELETE_ON_ERROR:
