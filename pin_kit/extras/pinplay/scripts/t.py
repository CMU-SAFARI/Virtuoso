user_pattern = '[a-zA-Z0-9-+%]+\.[a-zA-Z0-9-+%]+_[0-9]+'
if config.global_regions:
  pp_iregion_pattern = '_globalr([0-9]+)_warmup([0-9]+)_prolog([0-9]+)_region([0-9]+)_epilog([0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
else:
  pp_iregion_pattern = '_t([0-9])+r([0-9]+)_warmup([0-9]+)_prolog([0-9]+)_region([0-9]+)_epilog([0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
if config.global_regions:
  pp_pcregion_pattern = '_globalr([0-9]+)_warmupendPC(0x[0-9A-Fa-f]+)_warmupendPCCount([0-9]+)_warmuplength([0-9]+)_endPC(0x[0-9A-Fa-f]+)_endPCCount([0-9]+)_length([0-9]+)_multiplier([0-9]+-[0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
else:
  pp_pcregion_pattern = '_t([0-9])+r([0-9]+)_warmupendPC(0x[0-9A-Fa-f]+)_warmupendPCCount([0-9]+)_warmuplength([0-9]+)_endPC(0x[0-9A-Fa-f]+)_endPCCount([0-9]+)_length([0-9]+)_multiplier([0-9]+-[0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
full_iregion_pattern = user_pattern + pp_iregion_pattern
full_pcregion_pattern = user_pattern + pp_pcregion_pattern

