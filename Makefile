include defines.mk
include product.mk

build_dir	?= build

ifndef PWD
PWD := $(shell pwd)
endif

ifndef top_dir
top_dir		:= $(shell realpath \
			--relative-to '$(PWD)/$(build_dir)' '$(PWD)')
else
top_dir		:= $(shell realpath \
			--relative-to '$(PWD)/$(build_dir)' '$(top_dir)')
endif

src_dir		?= $(top_dir)
inc_dir		?= $(top_dir)

ifndef dmklib_origsrc_dir
dmklib_origsrc_dir := $(shell realpath \
			--relative-to '$(PWD)' '$(PWD)/dmklib')
else
dmklib_origsrc_dir := $(shell realpath \
			--relative-to '$(PWD)' '$(dmklib_src_dir)')
endif

dmklib_build_dir = $(build_dir)/dmklib
dmklib_files	 = Makefile dmk.h libdmk.h libdmk.c
dmklib_src	 = $(addprefix $(dmklib_build_dir)/,$(dmklib_files))

clean_files	 =
clobber_files	 = $(clean_files) $(build_dir)
distclean_files	 = $(clobber_files) build build.*

skip_goals	 = clean clobber distclean show_package show_version
build_make_goals = $(or $(filter-out $(skip_goals),$(MAKECMDGOALS)), all)

build_make = $(MAKE) \
		-C '$(build_dir)' \
		-I '$(inc_dir)' \
		-f '$(src_dir)/Makefile.build' \
		'src_dir=$(src_dir)' \
		'inc_dir=$(inc_dir)'


$(build_make_goals): $(dmklib_src) FORCE | $(build_dir)
	$(build_make) $(build_make_goals)

$(build_dir) $(dmklib_build_dir):
	mkdir -p -- '$@'

# Prep dmklib here so vpath in Makefile.build doesn't get confused.
# Copy dmklib files under the build dirs so we can build multiple
# architectures simulateously in separate dirs.
$(dmklib_src): %: $(addprefix $($(dmklib_origsrc_dir)/),$(*F)) | \
							$(dmklib_build_dir)
	cp -p -- '$(dmklib_origsrc_dir)/$(*F)' '$@' 

clean:
	[ ! -d '$(build_dir)' ] || $(build_make) '$@'

clobber distclean:
	rm -rf -- '$(build_dir)'


show_package:
	@echo $(PRODUCT)

show_version:
	@echo $(VERSION)


.PHONY: FORCE all
.PHONY: clean clobber distclean show_package show_version
.DELETE_ON_ERROR:
