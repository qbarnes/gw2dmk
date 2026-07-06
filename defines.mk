define nl


endef

# Use sort function to remove duplicates.
scrub_files_call = $(foreach f,$(sort $(wildcard $(1))),$(RM) -r -- '$f'$(nl))
