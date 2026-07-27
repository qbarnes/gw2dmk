define nl


endef

# Use sort function to remove duplicates.
scrub_files_call = $(foreach f,$(sort $(wildcard $(1))),$(RM) -r -- '$f'$(nl))

# Return the first of the names in "$(1)" found in PATH, or empty.
find_tool_call = \
	$(firstword $(foreach t,$(1),\
		$(wildcard $(addsuffix /$(t),$(subst :, ,$(PATH))))))
