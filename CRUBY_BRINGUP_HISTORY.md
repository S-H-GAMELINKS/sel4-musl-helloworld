# CRuby on seL4/CAmkES 実装経緯まとめ

このファイルは、seL4 + CAmkES + musl libc 上でCRubyを動かすまでの
実装経緯、詰まった点、判断理由、現在の制限をまとめたものです。

## 目的

最初の目的は、単なる seL4 + CAmkES + musl libc の `Hello World` から
一歩進めて、CRubyをユーザーランドの基盤として使えるかを試すことでした。

現在の到達点は以下です。

```text
QEMU serial input
  -> CAmkES SerialServer
  -> Hello component stdin shim
  -> CRuby STDIN.gets
  -> CPIOから読み込んだRubyシェル
```

確認済みの実行例:

```text
Ruby shell loaded from CPIO
commands: help, echo, version
hello from ruby shell
4.0.5
Entering Ruby shell loop
ruby>
help
commands: help, echo, version
ruby>
echo typed from qemu
typed from qemu
ruby>
version
4.0.5
ruby>
```

## ビルド手順の見直し

最初に重要だったのは、古い `griddle` ベースの手順から離れることでした。

`vendor/camkes-project` 以下では `init-build.sh` と `ninja` によるビルドが
既に通っていたため、そちらを正としました。`build-hello` は以前の手順で
作られた空ディレクトリであり、実際のCAmkESビルドツリーではありません。

そのため、作業の中心は以下になりました。

```text
vendor/camkes-project/
  init-build.sh
  ninja
  ./simulate
```

ローカルの `apps/hello` は、CAmkES側の `projects/camkes/apps/hello` へ
symlinkして取り込む形にしました。

## CRubyソースの選択

CRubyのGit checkoutをそのまま使うと、`configure`、`config.guess`、
`config.sub` などの生成済みファイルが存在せず、`./autogen.sh` が必要に
なります。

この実験では、余計な生成手順を避けるためにRuby 4.0.5のrelease tarballを
使う方針にしました。tarballには必要なconfigure関連ファイルが含まれて
います。

主なconfigure設定は以下です。

```text
--host=x86_64-linux-musl
--disable-shared
--disable-install-doc
--with-compress-debug-sections=no
--without-gmp
--with-static-linked-ext
rb_cv_function_name_string=__func__
ac_cv_lib_z_uncompress=no
ac_cv_func_mremap=no
ac_cv_func_fork=no
ac_cv_func_vfork=no
ac_cv_func_popen=no
ac_cv_func_getpgrp=no
ac_cv_func_setpgid=no
```

困った点:

- `rb_cv_function_name_string=__func__` がないと、
  `RUBY_FUNCTION_NAME_STRING` 未定義でビルドに失敗することがありました。
- zlibを無効化しないと、`zlib.h` が見つからず失敗しました。
- `--with-thread=none` はRuby 4.0.5ではうまくいきませんでした。
  `thread_native.h` などがnative thread型を期待するためです。

そのため、CRubyはpthreadモデルのままビルドし、CAmkES component側で
単一スレッド用のpthread stubを提供する方針にしました。

## CRubyのリンク

CRubyは以下をCAmkES componentへリンクしています。

```text
build/ruby-sel4-musl/libruby-static.a
```

include pathは主に以下です。

```text
build/ruby-sel4-musl/.ext/include/x86_64-linux-musl
vendor/ruby/include
```

現在の互換層は以下の3ファイルに分けています。

```text
apps/hello/components/Hello/src/ruby_alloc.c
apps/hello/components/Hello/src/ruby_platform.c
apps/hello/components/Hello/src/ruby_pthread.c
```

これらはあくまで今回のbring-up用です。完全なPOSIX実装やpthread実装では
ありません。

## 実行時bring-upの流れ

### pthread

最初は `ruby_init()` に入った後で止まりました。CRubyの初期化にはpthread
周辺のAPIが必要だったため、`ruby_pthread.c` に単一スレッド用stubを追加
しました。

重要なのは、本物のスレッドを作ったふりをしないことです。例えば
`pthread_create` は同じスタック上でcallbackを実行せず、失敗を返すように
しています。CRuby側は別native threadを前提にしているため、ここを雑に
同期実行すると別の破綻を招きます。

### allocator / mmap

pthread stub後、次はRuby GCのheap page allocation付近で止まりました。
`addr2line` では `heap_page_allocate` が見え、`nm` では `malloc` は
上書きできていたものの、`mmap` / `munmap` はCAmkES/musl側に落ちている
ことが分かりました。

そのため `ruby_alloc.c` で以下を提供しました。

```text
malloc
calloc
realloc
free
posix_memalign
aligned_alloc
malloc_usable_size
mmap
__mmap
munmap
__munmap
```

現在のallocatorは固定サイズarena + free list再利用です。隣接free blockの
coalesceはまだありません。`munmap` はno-opです。Ruby GCが元の`mmap`
ポインタとは異なるpage trimming rangeを渡すことがあるため、単純に
`free` と同一視できないからです。

### randomness

次にCRubyは乱数を要求しました。`getrandom()` が失敗すると `/dev/urandom`
へfallbackし、CAmkES側のfile handlingで失敗しました。

現在は `ruby_platform.c` に以下のdeterministic placeholderを置いています。

```text
getrandom
getentropy
```

これはbring-up用であり、暗号学的に安全ではありません。

### tty probing

Rubyのstdio/tty probingで `ioctl` が呼ばれ、libsel4muslcsysの未実装pathに
落ちました。

現在は以下のように「TTYではない」と返しています。

```text
ioctl -> ENOTTY
isatty -> 0 / ENOTTY
```

これは本物のTTY実装ではなく、Ruby初期化を進めるための境界です。

### communication pipe

次に以下のエラーに到達しました。

```text
libsel4muslcsys: Error attempting syscall 290
libsel4muslcsys: Error attempting syscall 293
libsel4muslcsys: Error attempting syscall 22
<main>: [BUG] can not create communication pipe
```

x86_64では以下です。

```text
290 = eventfd2
293 = pipe2
22  = pipe
```

CRubyのpthread backendは `eventfd` を試し、失敗するとpipeへfallbackします。
現在は小さなfake `eventfd` と fake `epoll` を実装して、この初期化経路を
通しています。

これは一般的なevent subsystemではなく、現状のCRuby初期化を進めるための
最小実装です。

## CPIOによるRubyファイル読み込み

最初のCRuby実行は `rb_eval_string` でした。次に、ファイルとして用意した
Rubyコードを読み込む方向へ進めました。

選んだ方式はCPIOです。

```text
apps/hello/components/Hello/ruby/
  shell.rb
```

CMakeでこのディレクトリから `newc` CPIOを生成し、`xxd -i` でC配列化して
以下を生成します。

```text
apps/hello/components/Hello/src/ruby_archive.c
```

この中に以下が定義されます。

```text
ruby_cpio
ruby_cpio_len
```

`ruby_platform.c` はこのCPIOをread-onlyなファイル面として公開しています。

```text
open
openat
access
stat
lstat
fstat
fstatat
getcwd
read
pread
lseek
close
fcntl
```

ここで詰まった点として、`ruby_archive.c` がすでにsource treeに存在すると、
CMakeが普通のsource fileとして扱い、`shell.rb` を変更しても再生成されない
問題がありました。

これは以下のように、生成ファイルであることと依存ターゲットを明示して
対応しました。

```cmake
set_source_files_properties(${HELLO_RUBY_ARCHIVE_C} PROPERTIES GENERATED TRUE)
add_custom_target(hello_ruby_archive DEPENDS ${HELLO_RUBY_ARCHIVE_C})
add_dependencies(hello.instance.bin hello_ruby_archive)
```

## Rubyの `load` ではなくC側readにした理由

Ruby側の:

```ruby
load "/shell.rb"
```

も試しましたが、missing fileとは異なる挙動をしつつ、期待したtop-levelの
script bodyが実行されませんでした。

ここで `load` の内部挙動まで同時に追うと、CPIOファイル面の検証と問題が
混ざります。そのため現在は以下の流れにしています。

```text
C側で /shell.rb をopen/readする
読み込んだ文字列を rb_eval_string_protect に渡す
```

これにより、まず「CPIOからRubyコードを読める」ことを独立して確認しました。
Ruby自身の `load` 経路は別タスクとして残しています。

## CAmkES stack size

Rubyスクリプトが少し複雑になると、`pm_compile_node` 付近でfaultしました。
fault addressがCAmkES control stackのguard page付近だったため、C stack不足
と判断しました。

最初は以下を試しました。

```camkes
hello.stack_size = 1024 * 1024;
```

しかしこれはcontrol thread stackには効きませんでした。正しいkeyは以下です。

```camkes
hello._stack_size = 1024 * 1024;
```

生成後の `hello/camkes.c` には以下が出ます。

```text
ROUND_UP_UNSAFE(1048576, PAGE_SIZE_4K)
```

これでparser付近のstack faultは解消しました。

## Rubyシェルの常駐化

最初の `shell.rb` は単にコマンドを実行して終了するスクリプトでした。
そのため `rb_eval_string_protect` が戻り、`hello.c` は `ruby_finalize()` まで
進んでいました。

シェルとしては戻ってはいけないので、`shell.rb` をループする形にしました。

```ruby
while true
  puts "ruby> "
  line = STDIN.gets
  ...
end
```

ここでも詰まった点があります。

- `loop do` は現在の移植状態ではKernel method解決で失敗したため、
  構文である `while true` を使いました。
- `Thread.pass` も避けました。thread周りはstubであり、本物のthread runtime
  ではないためです。

## stdin対応

QEMUから入力しても、そのままCRubyのfd 0へ届くわけではありませんでした。
初期状態ではstdinは未接続で、libsel4muslcsysの `readv` はfd 0に対して
未実装assertになります。

現在はCAmkESの既存global componentを使っています。

```camkes
import <SerialServer/SerialServer.camkes>;
import <TimeServer/TimeServer.camkes>;
import <global-connectors.camkes>;
```

`Hello` は以下を持ちます。

```camkes
uses GetChar stdin_getchar;
```

assemblyでは以下で接続します。

```camkes
connection seL4SerialServer stdin_input(from hello.stdin_getchar, to serial.getchar);
```

`ruby_platform.c` の `readv(STDIN_FILENO, ...)` は以下で待ちます。

```text
stdin_getchar_notification()
```

そして以下の共有リングバッファから読みます。

```text
stdin_getchar_buf
```

QEMU/SerialServer側ではEnterが `'\r'` として届くため、CRubyの
`STDIN.gets` 用に `'\n'` へ変換しています。

## 入力エコー

stdinが通った後も、入力中の文字は画面に表示されませんでした。これは普通の
Linux端末ならTTY driverが担当するecho機能が、今回の構成には存在しないためです。

現在は `readv(STDIN_FILENO, ...)` のshim内で簡易エコーしています。

```text
通常文字         -> そのままstdoutへ表示
Enter            -> 表示は "\r\n"、Rubyへ渡す値は "\n"
Backspace/Delete -> 表示は "\b \b"
```

これは本物のTTY line disciplineではありませんが、QEMU上でRubyシェルを
触るには十分です。

## 現在の制限

現在分かっている制限です。

- `getrandom` / `getentropy` はdeterministic placeholderであり安全ではありません。
- pthreadは単一スレッドstubであり、本物のthread実装ではありません。
- fake `eventfd` / `epoll` は現在のCRuby初期化経路を通すための最小実装です。
- allocatorは固定arena + free listで、隣接free blockのcoalesceはありません。
- `munmap` はno-opです。
- Ruby自身の `load "/shell.rb"` はまだ使っていません。
- stdin echoは簡易実装であり、TTY line editingではありません。
- `ruby_init()` 前後で出る syscall `107` / `108` などはまだ分類が必要です。

## 得られた方針

今回うまく進められたのは、問題を一つずつ具体的に切り分けたためです。

```text
build failure
  -> configure/cache変数やinclude/lib pathを見る
link failure
  -> missing symbolを見る
runtime assertion
  -> syscall番号やlibsel4muslcsysの該当箇所を見る
runtime fault
  -> addr2lineと生成されたCAmkES layoutを見る
```

特に役に立った確認は以下です。

```text
ninja -v
nm -u libruby-static.a
nm -n hello.instance.bin
addr2line -e hello.instance.bin ...
rg in generated hello/camkes.c
timeout ./simulate
```

重要なのは、足りないPOSIX APIを何でも雑にstubし続けないことです。現在の
shimはbring-upのための境界です。今後はそれぞれについて、以下のどれにするか
判断する必要があります。

```text
本物のseL4 serviceとして実装する
CRuby configure/build optionで無効化する
この環境ではunsupportedとして明示する
```
