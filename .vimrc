"色分けオプション
set term=builtin_linux
set ttytype=builtin_linux
colorscheme waco
syntax on

"整形オプション
set autoindent
set tabstop=2
set shiftwidth=2
set expandtab

"検索オプション
set incsearch
set hlsearch

"全角記号対策
set ambiwidth=double

"表示行単位で行移動する
map <up> gk
map <down> gj
nmap k gk
nmap j gj
vmap k gk
vmap j gj

"カーソルライン
set cursorline
"コマンド実行時などに自動保存
set autowrite
"保存しなくてもファイルを開く
set hidden
"ステータスラインを常に表示
set laststatus=2

function! GetB()
  let c = matchstr(getline('.'), '.', col('.') - 1)
  let c = iconv(c, &enc, &fenc)
  return String2Hex(c)
endfunction
" :help eval-examples
" The function Nr2Hex() returns the Hex string of a number.
 func! Nr2Hex(nr)
   let n = a:nr
     let r = ""
  while n
    let r = '0123456789ABCDEF'[n % 16] . r
    let n = n / 16
  endwhile
  return r
endfunc
" The function String2Hex() converts each character in a string to a two
" character Hex string.
func! String2Hex(str)
  let out = ''
  let ix = 0
  while ix < strlen(a:str)
    let out = out . Nr2Hex(char2nr(a:str[ix]))
    let ix = ix + 1
  endwhile
  return out
endfunc

if winwidth(0) >= 120
  set statusline=%<[%n]%m%r%h%w%{'['.(&fenc!=''?&fenc:&enc).':'.&ff.']'}%y\ %F%=[%{GetB()}]\ %l,%c%V%8P
else
  set statusline=%<[%n]%m%r%h%w%{'['.(&fenc!=''?&fenc:&enc).':'.&ff.']'}%y\ %f%=[%{GetB()}]\ %l,%c%V%8P
endif

"autocomlpop用
inoremap <expr> <cr> pumvisible() ? "\<c-y>\<cr>" : "\<cr>"
inoremap <expr> <left> pumvisible() ? "\<c-y>\<left>" : "\<left>"
inoremap <expr> <up> pumvisible() ? "\<c-y>\<up>" : "\<up>"
inoremap <expr> <down> pumvisible() ? "\<c-y>\<down>" : "\<down>"

"<TAB>で補完 
function InsertTabWrapper() 
  if pumvisible() 
    return "\<c-n>" 
  endif 
  let col = col('.') - 1 
  if !col || getline('.')[col -1] !~ '\k\|<\|/' 
    return "\<tab>" 
  elseif exists('&omnifunc') && &omnifunc == '' 
    return "\<c-n>" 
  else 
    return "\<c-x>\<c-o>" 
  endif 
endfunction 
inoremap <tab> <c-r>=InsertTabWrapper()<cr> 
