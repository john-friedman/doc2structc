We get 500mb - 3gb/s


okay whats next setup our core logic.

gcc -O2 -msse2 -o tokenize src/main.c src/tokenizer.c
./tokenize.exe tesla10k.html

## Tokenization

Tags:
- b, strong
- i, e
- u, ins
- tables
- image
- links

CSS Style
- italic: 'italic', 'oblique', 'normal'
- bold: font weight bold or like font-weight above 400
- font size with standard size
- underline: text decoration
- text align center
- text-indent / padding
- display none 
- normalization

Text Style 
- proper case
- all caps
- ignore stem words

## Hierarchy

## technique

user submits rules: parse

e.g. attribute idk b -> bold, strong -> bold. so tags are done dynamically, same with style


# Misc
- added implicit closing for html tags
- implicit closing problem limited to sec types (html 3.2)
## todo
better terminology eg css and text distinguished
