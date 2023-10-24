#### レジスタ

_RMP_BASE_ ... RMP 用領域の始端アドレス (Page-Aligned)
_RMP_END_ ... RMP 用領域の終端アドレス (Page-Aligned)

(_RMP_END_ - _RMP_BASE_) / (RMPE サイズ) \* 4096 = RMP に守られた物理メモリの上限

#### RMP エントリ(RMPE)の構造

RMP に守られた物理メモリにはページ毎に RMPE エントリが割り当てられ、以下のフィールドを保持する。

|             |                                                                                                                                                         |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| _ASID_      | 対象の VM の ID (0 は VMM に割当)                                                                                                                       |
| _TYPE_      | 割当の種類(_SHARED_\|_PRIVATE_\|_MERGEABLE_\|_LEAF_)                                                                                                    |
| _GPA_       | type=_MERGEABLE_ かつ _FIXED_=1 のとき：RMPE Leaf への物理アドレス<br>type=_MERGEABLE_ かつ _FIXED_=0 、又は type=_PRIVATE_：割り当てた VM における gPA |
| _VALIDATED_ | VM からの PVALIDATE 命令が成功するとセットされ、VMM による RMPUPDATE でクリアされる                                                                     |
| _FIXED_     | VMM による PFIX 命令が成功するとセット<br>_FIXED_=1 のときは _VALIDATED_ は常に 1                                                                       |

#### RMP Leaf の構造

ASID をインデックスとした 8 バイトのエントリを 512 個保持する。
ある VM に対応する物理ページが PMERGE されると、ASID からエントリを参照し、その gPA が書き込まれる。その際 x86 の Present ビットと同様に 0 ビット目がセットされる。

#### gPT 及び nPT の拡張

各 PTE の[53\:52]は割当の種類(_SHARED_\|_PRIVATE_\|_MERGEABLE_\|_LEAF_)を表す

#### RMPUPDATE

引数： _HPA_, _GPA_, _ASID_, _TYPE_

- VMM のみにより実行可能
- _HPA_ に対応する RMPE の各フィールドを更新する
- 成功時には RMPE 中の _VALIDATED_ はクリアされる
- 既に _TYPE_=LEAF である RMPE は更新できない
- ASID を変更する場合は対応する物理ページはゼロ埋めされる

#### PVALIDATE

引数： _GVA_, _TYPE_

- VM のみのより実行可能
- _GVA_ に対応する RMPE の _VALIDATED_ をセットする
- RMPE 内の _TYPE_ が _TYPE_ と一致しない場合エラー

#### PFIX

引数： _HPA_, _LEAF_

- VMM のみにより実行可能
- _HPA_ が指す物理ページを読み込み専用にし、_LEAF_ が指す物理ページを RMP Leaf として使用する
- _HPA_ に対応する RMPE の _FIXED_ がセットされ、_GPA_ には _LEAF_ が代入され、_ASID_ を RMPE Leaf のインデックスとして参照したエントリ内に _GPA_ の値が入る
- _HPA_ に対応する RMPE は _TYPE_=_MERGEABLE_ かつ _FIXED_=0 かつ _VALIDATED_=1 である必要がある
- _LEAF_ に対応する RMPE は _TYPE_=*LEAF*である必要がある
- TLB はクリア

#### PMERGE

引数： _HPA1_, _HPA2_

- VMM のみにより実行可能
- _HPA1_ が指す物理ページに _HPA2_ が指す物理ページをマージする
- _HPA1_ に対応する RMPE の _GPA_ 及び _HPA2_ に対応する RMPE の ASID から RMP Leaf エントリを参照し、_HPA2_ に対応する RMPE の _GPA_ を書き込む
- マージ後の _HPA2_ が指す物理ページはゼロ埋めされる
- VMM はその後 nPT エントリ内の hPA を変更することが期待される
- _HPA1_ と _HPA2_ が指す物理ページの内容は同じである必要がある
- _HPA1_ と _HPA2_ に対応する RMPE は _TYPE_=_MERGEABLE_ かつ _VALIDATED_=1 である必要がある
- _HPA1_ に対応する RMPE は _FIXED_=1、 _HPA2_ に対応する RMPE は _FIXED_=0 である必要がある
- TLB はクリア

#### PUNMERGE

引数： _HPA1_, _HPA2_, _ASID_

- VMM のみにより実行可能
- _HPA1_ が指す _FIXED_ な物理ページからページの内容を _HPA2_ が指す位置にコピーし、それを _ASID_ に対応する VM のみからアクセスできるようにする
- _HPA1_ に対応する RMPE の _GPA_ と _ASID_ から RMP Leaf のエントリを参照し、そこに記された gPA 及び _ASID_ を、 _HPA2_ に対応する新たに作成された RMPE にコピーし、エントリを削除する。
- VMM はその後 nPT エントリ内の hPA を変更することが期待される
- 新たに作成された RMPE は _VALIDATED_=1, _TYPE_=_MERGEABLE_, _FIXED_=0 となる
- _HPA1_ に対応する RMPE は _MERGEABLE_=1 かつ _FIXED_ =1 である必要がある
- _HPA1_ に対応する RMPE の _GPA_ の指す RMP Leaf 内の _ASID_ が指すエントリが存在する必要がある
- _HPA2_ に対応する RMPE は _TYPE_=_SHARED_ である必要がある
- TLB はクリア

#### PUNFIX

引数：_HPA_

- VMM のみにより実行可能
- _MERGEABLE_ なページの読み取り専用状態を解除し、RMP Leaf に使用されていたページを開放する
- _HPA_ に対応する RMPE の _ASID_ と _GPA_ から RMP Leaf エントリを辿り、gPA を RMPE 内の _GPA_ にセットし、RMP Leaf に使用されたページに対応する RMPE は _TYPE_=_SHARED_ とする
- _HPA_ に対応する RMPE の _ASID_ と _GPA_ から参照する RMP Leaf エントリが存在する必要がある
- _HPA_ に対応する RMPE は _FIXED_=1 である必要がある

#### メモリ参照

- RMP 用領域への書き込みは不可
- RMP に保護された領域外へのアクセスはいずれも可
- gPTE と nPTE の[53\:52]からメモリアクセスタイプ(_SHARED_ | _PRIVATE_ | _MERGEABLE_)決める
- メモリアクセスタイプと RMPE の _TYPE_ が異なる場合はエラー
- _FIXED_=1 のページへの書き込みは不可
- _PRIVATE_=1 又は、 _MERGEABLE_=1 かつ _FIXED_=0 のページでは _GPA_ とメモリアクセス時の gPA が同じであることを確認
- _MERGEABLE_=1 かつ _FIXED_=1 のページでは _GPA_ から RMP Leaf を辿り、その中に ASID に対応するエントリが存在することを確認し、それが指す gPA がメモリアクセス時の gPA と同じであることを確認

#### 考えられる攻撃とその対応

- _MERGEABLE_ / _PRIVATE_ なページの ASID 変更による他の VM からの参照
- _MERGEABLE_ / _PRIVATE_ なページを _SHARED_ に変更することによる他の VM/VMM からの参照
  - RMPUPDATE 時にページをゼロ埋め
- _MERGEABLE_ / _PRIVATE_ なページに対応する RMPE を VMM が更新し、そのページに対して他の gPA / VM から読み書きする (Memory Aliasing)
  - RMPUPDATE 時に _VALIDATED_=0 となるため書き込み時にメモリ参照失敗
- VMM が nPT を更新し、_MERGEABLE_ / _PRIVATE_ なページを他の hPA に割り当てる (Memory Re-mapping)
  - VM が 2 回 PVALIDATE を行わない限り VM からはメモリアクセスできない
- マージ済ページへの未登録な VM からのアクセス
  - RMP Leaf にエントリが存在せず、メモリ参照失敗
- 内容の異なるページのマージによるメモリ内容改ざん
  - PMERGE 失敗
- マージ済みのページへの VMM/VM からの改ざん
  - _FIXED_=1 によりメモリ参照失敗
- VMM が任意にエントリを編集したページを RMP Leaf として PFIX を行うことで、VM から PVALIDATE を行わずに gPA->hPA のマッピングを改ざんする
  - PFIX 時に RMP Leaf はゼロ埋めされる
- VMM による RMP Leaf の改ざん
  - _TYPE_=_LEAF_ のページは書き込み不可
- マージによって開放されたページの VMM / 他の VM からのアクセス
  - PMERGE 時にマージ元のページはゼロ埋めされる
- 悪意ある VM が他の VM のページの内容を予想し、その内容と同じページを用意し、VMM からの PMERGE による TLB キャッシュクリアを観測することで、他の VM のメモリ内容を観測する
  - 要検討
- 悪意ある VM が他の VM のページの内容を予想し、その内容と同じページを用意し、VMM による PMERGE を待った後に書き込みを行うことで CoW のレイテンシを観測し、他の VM のメモリ内容を観測する
  - 要検討
  - _MERGEABLE_ なページは常に読み込み専用にする?
