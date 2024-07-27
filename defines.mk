define nl
 
 
endef

scrub_files_call = $(foreach f,$(wildcard $(1)),$(RM) -r -- '$f'$(nl))
