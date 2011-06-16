augroup filetypedetectpython
" actionscript用がない
  au BufRead,BufNewFile *.as setfiletype javascript
  au BufRead,BufNewFile *.mxml setfiletype xml
augroup END
