# DRIVE Update V3 動作フロー

ソースコード（`sample_driveupdate.c`, `content_server.c`）の解析に基づく実行時の動作記録。

---

## アーキテクチャ概要

```
┌─────────────────────────────────────────────────────────────┐
│                        Thor (Target)                        │
│                                                             │
│  ┌──────────────────┐      DULINK      ┌────────────────┐  │
│  │ sample_driveupdate│◄────────────────►│   DU Master    │  │
│  │  (Update Client)  │                  │   (daemon)     │  │
│  └──────────┬────────┘                  └───────┬────────┘  │
│             │ DUCC (NvSCI IPC)                  │ DULINK    │
│             │  nvdu_gos_ipc_i_*                 │           │
│             └──────────────────────────►        │           │
│                                        ┌────────▼────────┐  │
│                                        │  content_server │  │
│                                        │ (Content Prov.) │  │
│                                        └─────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### コンポーネント

| コンポーネント | バイナリ | 役割 |
|---|---|---|
| DU Master | システムデーモン | 更新全体のオーケストレーション |
| Update Client | `sample_driveupdate` | 更新トリガー・状態監視 |
| Content Provider | `content_server` | パッケージファイルの DULINK 配信 |
| DU Link (DULINK) | ライブラリ | プロセス間通信バス (NvSCI ベース) |
| DUCC | ライブラリ | 状態・ランレベル管理 API |

---

## Phase 0: 前提 — content_server の起動

`content_server <dupkg_path>` を実行すると：

1. DULINK に `content-server` という名前で接続
2. 以下のノードを DULINK 空間にエクスポート

   | ノード | 属性 | 内容 |
   |---|---|---|
   | `plugin-type` | 読み取り専用 | `"content-provider"` |
   | `requested_rl` | Master のみ書き込み | ランレベル要求受信 |
   | `current_rl` | 読み取り専用 | 現在のランレベル |
   | `cmd` | 読み書き | `serve`/`stop_serve` コマンド受付 |
   | `state` | 読み取り専用 | `idle` / `configured` |
   | `persistent_ctx_path` | 読み書き | コンテキスト保存パス |

3. DU Master に **プラグインとして登録** (`deregister` まで有効)
4. Context Store パスを Master から受信するまで待機
5. DUPKG フォルダ内の `list_of_files.json` を読み込み、各ファイル・ディレクトリを `/content/files/...` として DULINK 空間にエクスポート
6. Master からのファイル読み出し要求（`exportFileCB`）を待機状態に入る

---

## Phase 1: sample_driveupdate の起動

`sample_driveupdate` を起動すると：

1. **DULINK 接続の初期化** (`initDulinkConn`)
   - `du-client` という名前で DULINK に接続
   - 自身のノードをエクスポート（`plugin-type`, `requested_rl`, `current_rl`, `pending_rl`, `state`）
   - DU Master にプラグインとして登録 (`registerToMaster`)

2. **DUCC 接続の初期化** (`initDuccConn`)
   - NvSCI IPC エンドポイント `nvdu_gos_ipc_i_1` / `nvdu_gos_ipc_i_0` 経由で Master に接続
   - `DUCC_Get_Current_RunLevel()` が成功するまでリトライ（最大 60 回、1 秒間隔）
   - 失敗時: `"make sure sample is running on master Tegra!"` と出力して終了

3. 対話型 CLI が起動し、コマンド入力待ちになる（プロンプト: `(client)`）

> **前回の実行コンテキストが残っている場合**: 起動時に DULINK から persistent context を読み込み、自動的に `monitorUpdate` スレッドを再開する。

---

## Phase 2: 更新トリガー (`package /content/files`)

`deployPackage()` の実行手順：

### 2-1. 事前チェック

```
/content/files  ←  dulinkGetAttribute() でディレクトリ存在確認
```

- パスが存在しない → エラー終了
- パスがディレクトリでない → エラー終了
- `--auth` フラグあり: `<pkg>/auth_conf.json` の存在を確認

### 2-2. Push モードの準備（`--push` フラグ時のみ）

1. `push_cb` ノードを DULINK にエクスポート（write-only コールバック）
2. `/tii/push_mode.notify` に `+<parent>/du-client/push_cb` を書き込み、コールバックを登録
3. DU TII が push_mode を変化させたとき、このコールバックが呼ばれる

### 2-3. DUCC 状態確認

デプロイ可能な状態：`STATE_DORMANT` / `STATE_UPDATE_FAILED` / `STATE_UP_TO_DATE`

これ以外の状態（進行中など）の場合は警告を出して何もしない。

### 2-4. 更新トリガー送信

DU Master の cmd ノード (`/master/cmd`) へ DULINK write：

```
# Pull モード（通常）
deploy metadata=<pkg>/du_master.json content_root=<pkg>

# Auth パッケージ
deploy metadata=<pkg>/du_master.json auth=<pkg>/auth_conf.json content_root=<pkg>
```

### 2-5. コンテキスト保存

DULINK の persistent_ctx_path に JSON を書き込み（再起動後の resume に使用）：

```json
{"pkgPath": "/content/files", "bPush": 0}
```

### 2-6. 監視スレッド起動

`pthread_create` で `monitorUpdate` スレッドを起動し、CLI はユーザー入力の受付を継続。

---

## Phase 3: DU Master による処理

Master が `deploy` コマンドを受信すると（Master 内部の処理）：

1. `du_master.json` を読み込んでパッケージ構造を把握
2. content_server が配信している `/content/files/...` からファイルを DULINK 経由で読み出し
3. インストーラプラグインへ `requested_rl` を送信してランレベルを要求
4. 各プラグインが runlevel に応じた処理を実行

---

## Phase 4: 更新進捗の監視 (`monitorUpdate` スレッド)

1 秒ごとに `DUCC_Get_Current_Status()` を呼び出してステートを確認：

```
STATE_DORMANT           → 待機中
STATE_NO_CONNECTIVITY   → 接続なし
STATE_UPDATE_AVAILABLE  → パッケージ検出済み、未開始
STATE_UPDATE_IN_PROGRESS → インストール中（進捗 % を表示）
STATE_UPDATE_FAILED     → 失敗 → ループ終了
STATE_FATAL_ERROR       → 致命的エラー → ループ終了
STATE_UP_TO_DATE        → 完了 → ループ終了
```

### Pending Action の処理

`PA_RUNLEVEL`（Master がより高いランレベルを要求）:

| モード | 動作 |
|---|---|
| 通常（auto）| `DUCC_Request_RunLevel()` を自動呼び出し |
| `--target-controlled` | `"Accept RUNLEVEL increase request? (y/n)"` と表示してユーザー確認 |

---

## Phase 5a: Pull モード — ファイル転送

DU Master が `/content/files/<filename>` に対して DULINK read を発行すると：

```
Master ──dulinkRead(/content/files/xxx)──► content_server
                                              ↓ exportFileCB()
                                         ローカルファイルをopen/seek/read
                                              ↓
Master ◄──────────────────────────────── データ返却
```

`exportFileCB` の動作：
- `DULINK_CB_READ`: `fopen` → `fseek(offset)` → `fread` → バッファ返却
- `DULINK_CB_SIZE`: `stat()` でファイルサイズ返却
- `DULINK_CB_WRITE`: `fwrite`（差分適用などで使用）

---

## Phase 5b: Push モード — パーティション直接書き込み

`--push` フラグ使用時の追加フロー：

```
DU TII
  └─ /tii/push_mode = "enabled" を書き込む
       └─ sample_driveupdate の pushCB が呼ばれる
            └─ performPush() スレッドを起動
```

`performPush()` の処理：

1. `/tii/ver/active_bootchain` を読んでアクティブなブートチェーンを確認（A=0, B=1）
2. 非アクティブ側のチェーン（例: A がアクティブなら B-chain）を更新対象とする
3. `<pkg>/tii-a/push_image_map.json` を読み込んでイメージ→パーティションのマッピングを取得
4. 各エントリを順番に処理：

   | エントリタイプ | 処理 |
   |---|---|
   | `image` + `partition` | content_server からイメージ読み出し → パーティションに直接 write |
   | `validate_p` | push_mode="validating" → TII の検証完了を待機（condvar） |
   | `reload` | DU-TII RPE にリロードコマンドを送信 |

5. 全処理成功 → `/tii/push_mode` = "disabled" を書き込む
6. 失敗 → `/master/cmd` に "abort" を書き込む

---

## Phase 6: 完了

### 成功

```
[Result]Deploy OK!
```

- persistent context をクリア
- `/master/persistent_log.list` の内容を表示
- `/tii/cmd.log` の内容を表示

### 失敗

```
[Result]Deploy FAILURE!
```

- 上記ログを同様に表示してデバッグ情報を提供

---

## 状態遷移まとめ

```
起動
  │
  ├─ initDulinkConn()  ── DULINK 接続・DU Master 登録
  ├─ initDuccConn()    ── DUCC 接続（最大 60 秒リトライ）
  │
  ▼
CLI 待機 (STATE_DORMANT)
  │
  │  package /content/files
  ▼
deployPackage()
  ├─ パス確認
  ├─ Push CB 登録（--push 時）
  ├─ dulinkWrite(MASTER_CMD, "deploy ...")
  └─ monitorUpdate スレッド起動
       │
       ├─ STATE_UPDATE_IN_PROGRESS → 進捗表示
       │    └─ PA_RUNLEVEL → DUCC_Request_RunLevel()
       │
       ├─ STATE_UP_TO_DATE  ──→ "[Result]Deploy OK!"
       └─ STATE_UPDATE_FAILED ─→ "[Result]Deploy FAILURE!"
```

---

## 中断（abort）

`abort` コマンドを実行すると：

1. Push CB の登録解除（`/tii/push_mode.notify` から削除）
2. 現在の状態が `STATE_UPDATE_IN_PROGRESS` または `STATE_UPDATE_AVAILABLE` なら `/master/cmd` に "abort" を書き込む
3. 状態が `STATE_UPDATE_FAILED` になるまで 1 秒ごとにポーリング
4. persistent context クリア
5. `DUCC_Request_RunLevel(RL_DORMANT)` でランレベルを Dormant に戻す

---

## 注意点

- **DU Master が起動していないと** `initDuccConn()` が 60 秒タイムアウト後に失敗して終了する（現在の Thor 環境では該当）
- **content_server と sample_driveupdate は独立したプロセス**。content_server が先に起動して DULINK 空間にファイルを公開しておく必要がある
- Push モードは **TA（Tegra-A）のみ対応**（サンプルコードのコメントより）
- ランレベルは 0 〜 6 の範囲で管理される（`RL_DORMANT` = 0）
