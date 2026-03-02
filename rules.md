just get it out

lets do uhh italic, and underline? 
then bold....


Instructions List
- instruction - how a piece of text on same block/line looks visually
- instruicton block - all instructions  on same block/line
- instruction block array - all instruction blocks

misc:

- merge_instructions_within_instruction_block
- fix_fake_tables
- "whitespace": ['replace_controls','collapse_spaces','trim'] 
- "collapse_tables" : True
- 

after tokenization what is hformat do we want

 # YES #
string, matrix, important attributes mask
also attributes mask id to attributes. this seperate compute from storage?

so what we need is the  mappoing in our head of what users can define as features
e.g. proper case all caps, tags: bold, style bold

level mappings via regex

# also:
vis instructions
viz result?

# OKAY SO WE LET USES DEFINE WHICH FEATURES THEY WANT, NORMALIZATION, and RULES for hierarchy


just use a json for input for now
also ehr um we now for tables cells should do like

instrucion is a cell, with colspan rowspan attribute and row or coulmn not sure. but this is how we make it flat


font size - h1, h2,....

# I DO Like seperation of adapter and instructions