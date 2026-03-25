# DRIVE Update V3 テスト

DRIVE Update V3 の基本動作（パッケージ配布・更新適用）を検証するプロジェクト。

## アーキテクチャ

```
[x86 ホスト]                        [Thor ターゲット (aarch64)]

  build.sh                            DU Master (システムデーモン)
  (Docker クロスコンパイル)                 |
       |                                  | DU Link (IPC)
       v                            ------+------+----------+
     bin/                           |            |          |
  ┌──────────────┐             content_server  driveupdate  duinstaller
  │content_server│  ──scp──>   (.dupkg を         (更新          (インストーラ
  │driveupdate   │             DU Link で          クライアント)    プラグイン)
  │du_cli        │             配信)
  │duinstaller   │  ──scp──>   du_cli
  └──────────────┘             (CLI 操作)
```

### コンポーネント一覧

| バイナリ | ソース | 役割 |
|----------|--------|------|
| `content_server` | `src/content_server/` | `.dupkg` パッケージを DU Link 経由で配信するコンテンツプロバイダ |
| `driveupdate` | `src/driveupdate/` | DU Master に接続し、コンテンツ取得・更新適用フローを駆動するクライアント |
| `du_cli` | `src/du-cli/` | DU Server に直接接続してパーティションにペイロードを書き込む低レベル CLI |
| `duinstaller` | `src/duinstaller/` | deploy/commit コマンドを受け取りファイルをアトミックに書き込むインストーラプラグイン |

---

## コンポーネント詳細

### content_server

**役割:** `.dupkg` パッケージファイルを DU Link IPC トランスポート経由で配信するコンテンツプロバイダプラグイン。DU Master に登録し、他のプラグイン（`driveupdate` など）がファイルを取得できるようにする。

**動作原理:**

1. `.dupkg` ディレクトリ内の `list_of_files.json` を読み込み、公開するファイル/ディレクトリを特定する。
2. 各エントリを `/content/files/` 以下の DU Link ノードとしてエクスポートする。
3. DU Master からの読み取りリクエストに対し、ローカルファイルの内容を DU Link 経由でストリーミングする。

**インタラクティブ CLI コマンド:**

| コマンド | 説明 |
|---------|------|
| `serve [<dupkg>]` | 指定した DUPKG の配信を開始（省略時はコンテキストストアから復元） |
| `stop` | 配信停止。エクスポートした全ノードをアンリンク |
| `status` | 配信状態・コンテキストストアパス・キャッシュパスを表示 |
| `set <dupkg>` | DUPKG パスをローカルにキャッシュ（配信は開始しない） |
| `save <dupkg>` | DUPKG パスをコンテキストストアに永続化 |
| `clear` | コンテキストストアから DUPKG パスを削除 |
| `quit` | 終了 |

**起動オプション:**

```
content_server <dupkg_path>   # 指定した DUPKG を即時配信
content_server                # コンテキストストアから前回の DUPKG を復元して配信
content_server -r             # 上と同じ（明示的な resume）
content_server -i             # パッケージなしで idle モード起動
content_server -c             # コンテキストストアをクリアして終了
```

---

### driveupdate

**役割:** DU Link および DUCC（コマンド＆コントロール API）経由で DU Master に接続し、パッケージデプロイをトリガーして更新進捗を監視するクライアント。

**動作原理:**

1. DU Link と DUCC で DU Master に接続。
2. `deploy metadata=<path>/du_master.json content_root=<path>` コマンドを DU Master に送信。
3. バックグラウンドスレッドで `DUCC_Get_Current_Status()` を監視し、DU Master の要求に応じて自動的にランレベルを上げる。
4. 状態が `UP_TO_DATE` または `UPDATE_FAILED` になると終了。

**サブコマンド（インタラクティブまたは CLI フラグで単発実行）:**

| コマンド | 説明 |
|---------|------|
| `package <dulink_path> [options]` | 指定した DU Link パスの DUPKG をデプロイ |
| `abort` | 進行中のデプロイを強制中断 |
| `query_bootchain` | 各 Tegra のブートチェーン（A/B）を表示 |
| `part_ver` | PVIT から全パーティションのバージョンを表示 |
| `get_runlevel` | 現在のランレベルを表示 |
| `set_runlevel <0-6>` | ランレベルを設定 |
| `print_status on\|off` | 定期的なステータス表示の有効/無効 |
| `du_read <node> [dst]` | DU Link ノードを読み取り（オプションでファイルに保存） |
| `du_write <data> <node>` | DU Link ノードにデータを書き込み |
| `du_list <dir>` | DU Link ディレクトリの子ノードを一覧表示 |
| `shell <cmd>` | シェルコマンドを実行 |
| `exit` | インタラクティブシェルを終了 |

**`package` オプション:**

| オプション | 説明 |
|-----------|------|
| `-t, --auth` | 認証チェックを強制（`auth_conf.json` が必須） |
| `-u, --push` | プッシュモード: クライアントがストレージパーティションに直接書き込む |
| `-d, --delta` | bsdiff を使ったデルタプッシュモード |
| `-c, --target-controlled` | ランレベル上昇前にユーザー確認を求める |

**使用例:**

```
# /content/files に公開済みのパッケージをデプロイ
driveupdate package /content/files
```

---

### du_cli

**役割:** DU Server (DUS) デーモンに直接接続し、パーティションペイロードを書き込む低レベル CLI ツール。`driveupdate` と異なり DU Master を経由せず、`duscExecute` API 経由で非アクティブブートチェーンに直接書き込む。

**動作原理:**

1. `<dupkg_dir>` 内の連番ペイロードファイル（`1.l1pt`, `1.pt`, `1.patch`, `2.pt` …）をスキャン。
2. L1 パーティションテーブル（`.l1pt`）を persistent チェーンに、通常ペイロード（`.patch`）を非アクティブブートチェーンに書き込む。
3. PT 書き込み後にパーティションテーブルをリロード。
4. 進捗（パーセント・速度・経過時間）を stdout に出力。
5. 完了後、オプションで DU Server ログを取得・保存。

**オプション:**

| オプション | 説明 |
|-----------|------|
| `-d <dupkg_path>` | OTA ペイロードファイルが格納されたディレクトリ |
| `-l <log_path>` | DU Server ログの保存先（省略可） |
| `-h` | ヘルプを表示 |

**使用例:**

```
du_cli -d /path/to/dupkg
du_cli -d /path/to/dupkg -l /tmp/dus.log
```

---

### duinstaller

**役割:** DU Master から DU Link 経由で `deploy` / `commit` コマンドを受け取り、更新ファイルをローカルファイルシステムにアトミックに書き込むインストーラプラグイン。

**コマンド動作:**

- `deploy path=<DU_LINK_PATH> savename=<name>`: DU Link の `path` からファイルを読み取り `<installDir>/<name>.staged` に書き込む。
- `commit`: `<name>.staged` → `<name>` にアトミックリネーム。
- `clear_error`: `.staged` ファイルを削除して IDLE に戻る。

**ステートマシン:**

```
IDLE ──[deploy]──> PENDING_RL ──[RL ≥ RL_BG_APPLY]──> INSTALLING
                                                             |
                                                       [deploy 完了]
                                                             v
IDLE <──[commit 完了]────────────────────── PENDING_COMMIT

任意の状態 ──[エラー]──> ERROR ──[clear_error]──> IDLE
```

**状態説明:**

| 状態 | 説明 |
|------|------|
| `IDLE` | `deploy` コマンド待ち |
| `PENDING_RL` | ランレベルが `RL_BG_APPLY` に達するのを待機 |
| `INSTALLING` | deploy 実行中: DU Link からファイルをコピーして `<name>.staged` に保存 |
| `PENDING_COMMIT` | deploy 完了。`commit` コマンド待ち |
| `ERROR` | コマンドが失敗。`clear_error` のみ受け付ける |

**エクスポートする DU Link ノード:**

| ノード | 説明 |
|--------|------|
| `plugin-type` | `"installer"` |
| `cmd` | 書き込みコマンド: `deploy path=… savename=…`, `commit`, `clear_error` |
| `state` | 現在の状態文字列 |
| `progress` | インストール中の進捗文字列 |
| `current_rl` / `pending_rl` | ランレベル管理 |
| `ver/installer` | インストーラのバージョン文字列 |
| `last_deployed/metadata` | 最後にデプロイしたファイルのパス |
| `last_deployed/result` | `"success"` または `"failed"` |

---

## DUPKG の作成方法

`.dupkg` は DRIVE Update V3 が扱う更新パッケージのディレクトリ形式。
**`dupkg` ツール**（SDK 同梱の Python ツール）を使ってテンプレートから生成する。

### dupkg ツールのインストール（Docker 内で一度だけ実施）

```bash
source env.sh
docker exec -it ${DOCKER_CONTAINER} bash

# 依存パッケージのインストール
pip3 install jinja2 tabulate --break-system-packages

# dupkg ツールのインストール
cd /drive/drive-foundation/tools/driveupdate/dupkg
pip3 install -e . --break-system-packages

# 動作確認
dupkg --help
```

### テンプレートの確認

```bash
# 利用可能なテンプレート一覧
dupkg lstemplate

# 特定テンプレートの必須変数を確認
dupkg lsin --template dupkg_sample_template
```

### duinstaller 用パッケージの作成（`dupkg_sample_template`）

このプロジェクトの `duinstaller` は「ファイルを受け取って任意のパスに保存する」汎用インストーラ。
`dupkg_sample_template` を使って対応パッケージを生成する。

#### 必須変数

| 変数 | 説明 | 例 |
|------|------|-----|
| `SAMPLE_SRC` | インストールするファイルが格納されたディレクトリ | `/tmp/myfiles` |
| `SAMPLE_NAME` | インストールするファイル名 | `update.bin` |
| `DESTINATION_SRC` | Thor 上のインストール先パス（保存ファイル名） | `update.bin` |

#### 手順

```bash
# 1. インストールするファイルを用意
mkdir -p /tmp/myfiles
echo "hello update" > /tmp/myfiles/update.bin

# 2. パッケージを生成（Docker コンテナ内で実行）
dupkg gen \
  --template dupkg_sample_template \
  --in SAMPLE_SRC=/tmp/myfiles \
       SAMPLE_NAME=update.bin \
       DESTINATION_SRC=update.bin \
  --out /tmp/my_dupkg

# 3. 生成結果を確認
ls /tmp/my_dupkg/
```

生成後のディレクトリ構造:

```
my_dupkg/
├── du_master.json        # DU Master 向けオーケストレーション定義
├── list_of_files.json    # content_server が公開するファイル一覧
└── update.bin            # インストール対象ファイル
```

#### list_of_files.json の構造

`content_server` はこのファイルを参照して DU Link にエクスポートするノードを決定する:

```json
[
  {"name": "./",              "type": "dir"},
  {"name": "./du_master.json","type": "file"},
  {"name": "./list_of_files.json", "type": "file"},
  {"name": "./update.bin",    "type": "file"}
]
```

#### du_master.json の構造（sample テンプレート生成例）

```json
{
  "metaVersion": 3,
  "restoreSequence": ["*/ddu", "*/du-client", "*/content", "/auth", "/tii"],
  "actions": [
    {
      "actionId": 0,
      "pluginType": "installer",
      "pluginPath": "/plugin",
      "startOnState": ["IDLE"],
      "exitOnStateTransitionTo": ["PENDING_COMMIT"],
      "failOnStateTransitionTo": ["ERROR"],
      "cmd": "deploy path=$CONTENT_ROOT$/update.bin savename=update.bin"
    },
    {
      "actionId": 1,
      "gatedBy": [0],
      "pluginType": "installer",
      "pluginPath": "/plugin",
      "startOnState": ["PENDING_COMMIT"],
      "exitOnStateTransitionTo": ["IDLE"],
      "failOnStateTransitionTo": ["ERROR"],
      "cmd": "commit"
    }
  ]
}
```

> **`$CONTENT_ROOT$`** は実行時に DU Link の `/content/files` パスに展開される動的変数。

### 4. パッケージを Thor に転送して content_server で配信

```bash
# Docker からホストにコピー
docker cp ${DOCKER_CONTAINER}:/tmp/my_dupkg ./my_dupkg

# Thor に転送
scp -r ./my_dupkg ${THOR}:/home/autoware/my_dupkg

# content_server で配信
./driveupdate_test/scripts/run_server.sh /home/autoware/my_dupkg
```

### 参考: 主なテンプレート一覧

| テンプレート名 | 用途 |
|--------------|------|
| `dupkg_sample_template` | `duinstaller` (sample plugin) 用。任意ファイルのデプロイ検証に最適 |
| `dupkg_single_tegra_template` | 単一 Tegra の OTA（BSP 画像ファイルが必要） |
| `dupkg_dual_tegra_template` | TA + TB デュアル Tegra 構成の OTA |
| `dupkg_push_template` | Push モード更新（driveupdate `--push` オプション使用時） |
| `dupkg_clone_template` | アクティブチェーンを非アクティブチェーンにクローン |
| `dupkg_recovery_template` | Chain C リカバリ更新 |

> 詳細: [DRIVE Update V3 Developer Guide](https://developer.nvidia.com/docs/drive/drive-os/7.0.3/public/drive-os-linux-sdk/embedded-software-components/DRIVE_AGX_SoC/DRIVE_Update_V3/DRIVE_Update_V3_Developer_Guide.html)

---

## 前提条件

### ホスト

- Docker コンテナ `drive-sdk` が起動していること（DRIVE OS SDK イメージ）
- `~/.ssh/config` に `thor` として Thor の SSH アクセスが設定されていること

コンテナが起動していない場合:

```bash
source env.sh
docker ps | grep ${DOCKER_CONTAINER} || \
docker run -d --rm -it --privileged --net=host \
    -v /dev:/dev \
    -v $HOME/driveos_ws:/home/nvidia \
    --name ${DOCKER_CONTAINER} \
    nvcr.io/drive/driveos-sdk/drive-agx-linux-nsr-aarch64-sdk-build-x86:7.0.3.0-0010
```

### ターゲット (Thor)

- DRIVE OS が起動していること
- DU Master デーモンが動作していること
- デプロイディレクトリへのアクセスが可能なこと: `${THOR_DEPLOY_DIR}`（デフォルト: `/home/autoware/epl_test`）

---

## 環境設定

環境変数は `env.sh` で管理する。作業前に必ず source すること:

```bash
source env.sh
```

| 変数 | デフォルト | 説明 |
|------|-----------|------|
| `DOCKER_CONTAINER` | `drive-sdk` | クロスコンパイル用 Docker コンテナ名 |
| `THOR` | `thor` | ターゲットの SSH ホスト名 |
| `THOR_DEPLOY_DIR` | `/home/autoware/epl_test` | Thor 上のデプロイディレクトリ |

---

## ビルド

Docker コンテナ内で aarch64 向けにクロスコンパイルし、`bin/` にバイナリを出力する:

```bash
source env.sh
./build.sh
```

出力:

```
bin/
├── content_server
├── driveupdate
├── du_cli
├── duinstaller
├── libnvdubhc_api.so
├── ffu_update    # オプション（libnvmnand_private が必要）
└── remote_content_provider  # オプション（libcurl が必要）
```

---

## 実行

各スクリプトは以下の処理を自動で行う:

1. `bin/` に存在するバイナリを `scp` で Thor の `${THOR_DEPLOY_DIR}` に転送
2. `ssh` で Thor 上のバイナリを実行
3. 標準出力・標準エラーを `driveupdate_test/logs/` にタイムスタンプ付きで保存

> **事前条件:** `./build.sh` でビルド済みのバイナリが `bin/` に存在すること。

---

### 標準ワークフロー（DU Link 経由の更新）

`content_server` と `driveupdate` を組み合わせた標準的な DRIVE Update V3 フロー。
**ターミナルを 2 つ**使用する。

#### ステップ 1: Content Server を起動（ターミナル A）

```bash
source env.sh
# Thor 上の .dupkg パスを指定して配信開始
# 例: dupkg が Thor の /home/autoware/my.dupkg にある場合
./driveupdate_test/scripts/run_server.sh /home/autoware/my.dupkg
```

起動後、content_server の CLI プロンプト `content-server>` が表示され待機状態になる。

```
=== content_server: deploy and run ===
Thor: thor
Deploy path: /home/autoware/epl_test/content_server
Binary deployed to Thor.
Running: /home/autoware/epl_test/content_server /home/autoware/my.dupkg
CONTENT-SERVER[/home/autoware/my.dupkg]>
```

#### ステップ 2: Update Client を実行（ターミナル B）

```bash
source env.sh
# インタラクティブモードで起動（プロンプトから操作）
./driveupdate_test/scripts/run_client.sh

# または直接パッケージデプロイを指定して起動
./driveupdate_test/scripts/run_client.sh package /content/files
```

起動すると DU Master に接続し、`(client)` プロンプトが表示される。

```
=== driveupdate: deploy and run ===
Thor: thor
Deploy path: /home/autoware/epl_test/driveupdate
Binary deployed to Thor.
Running: /home/autoware/epl_test/driveupdate
(client)
```

プロンプトから手動でデプロイをトリガーする場合:

```
(client) package /content/files
```

更新が完了すると `[Result]Deploy OK!` または `[Result]Deploy FAILURE!` が表示されて終了する。

#### ステップ 3: ログの確認

```bash
# 最新のサーバーログ
ls -lt driveupdate_test/logs/server_*.log | head -1

# 最新のクライアントログ
ls -lt driveupdate_test/logs/client_*.log | head -1
```

---

### Content Server 詳細オプション

```bash
# 指定した DUPKG を即時配信
./driveupdate_test/scripts/run_server.sh /home/autoware/my.dupkg

# パッケージなしで idle モード起動（CLI から後で serve コマンドを実行）
./driveupdate_test/scripts/run_server.sh --idle

# コンテキストストアから前回の DUPKG を復元して配信
./driveupdate_test/scripts/run_server.sh --resume

# コンテキストストアをクリアして終了
./driveupdate_test/scripts/run_server.sh --clear
```

content_server の CLI（起動後のプロンプト）でも操作できる:

```
CONTENT-SERVER[/path]> serve /home/autoware/other.dupkg  # 別のDUPKGに切り替え
CONTENT-SERVER[/path]> stop                               # 配信停止
CONTENT-SERVER[/path]> status                             # 状態確認
CONTENT-SERVER[/path]> quit                               # 終了
```

---

### du_cli を使った低レベル OTA

DU Master を経由せず、DU Server に直接書き込む場合に使用する。

```bash
source env.sh

# OTA パッケージをデプロイ
./driveupdate_test/scripts/run_tool.sh -d /home/autoware/ota_pkg

# DU Server ログをファイルに保存しながらデプロイ
./driveupdate_test/scripts/run_tool.sh -d /home/autoware/ota_pkg -l /tmp/du.log
```

---

### スクリプトの動作詳細

各スクリプトは共通して以下の処理を行う:

```
[ホスト]                          [Thor]
  |                                  |
  |-- scp bin/<binary> -----------> mkdir -p ${THOR_DEPLOY_DIR}
  |                                  |  受信: ${THOR_DEPLOY_DIR}/<binary>
  |-- ssh <binary> [args] ---------> 実行
  |<-- stdout/stderr -------------- 出力
  |
  tee driveupdate_test/logs/<name>_YYYYMMDD_HHMMSS.log
```

| スクリプト | 転送バイナリ | ログファイル名 |
|-----------|------------|--------------|
| `run_server.sh` | `bin/content_server` | `logs/server_*.log` |
| `run_client.sh` | `bin/driveupdate` | `logs/client_*.log` |
| `run_tool.sh` | `bin/du_cli` | `logs/tool_*.log` |

---

## プロジェクト構成

```
.
├── env.sh                     # 環境変数
├── build.sh                   # クロスコンパイルスクリプト (host → Docker → bin/)
├── bin/                       # ビルド済み aarch64 バイナリ (gitignore)
├── src/
│   ├── common.mk              # 共通ツールチェーン/フラグ定義
│   ├── content_server/        # 更新パッケージサーバ
│   ├── driveupdate/           # 更新クライアント
│   ├── du-cli/                # CLI ツール
│   ├── duinstaller/           # インストーラプラグイン
│   │   ├── duinstaller.c/h            # ステートマシン + DU Link ノードテーブル
│   │   ├── duinstaller_helpers.c/h    # deploy / commit コマンド実行
│   │   └── duinstaller_main.c         # エントリポイント
│   ├── plugin_common/         # 共通インストーラコールバック (installerCmdCB 等)
│   ├── bhc_plugin/            # BHC API 共有ライブラリ
│   ├── remote_content_provider/  # リモートコンテンツプロバイダ (オプション、libcurl 必須)
│   └── ffu_update/            # FFU 更新ツール (オプション、libnvmnand_private 必須)
└── driveupdate_test/
    ├── scripts/
    │   ├── run_server.sh      # content_server を Thor にデプロイして実行
    │   ├── run_client.sh      # driveupdate を Thor にデプロイして実行
    │   └── run_tool.sh        # du_cli を Thor にデプロイして実行
    └── logs/                  # タイムスタンプ付き実行ログ (gitignore)
```
