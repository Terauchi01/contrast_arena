# contrast_arena 運用ガイド

このドキュメントは、現在のソケットベースのサーバ／クライアント構成を日常的に扱うための手順をまとめたものです。

## 1. サーバの起動方法

1. ビルド（変更が入ったときのみ）:
   ```bash
   make server
   ```
2. リポジトリ直下でサーバを起動（TCP `8765` を待ち受け）:
   ```bash
   ./server_app
   ```
3. 起動時に行われること:
   - サーバは正規の `contrast::GameState` を初期化し、ステータスを `ongoing` に設定します。
   - 新規 TCP 接続ごとに `INFO` 行→`STATE` ブロックを送信し、クライアントが自分のロールを把握できるようにします。
   - 受け付けるコマンドは `ROLE` / `MOVE` / `GET_STATE` の3種（いずれも改行終端の ASCII 文字列）。

## 2. クライアントの起動方法

1. ビルド（変更時のみ）:
   ```bash
   make client
   ```
2. 起動シンタックス:
   ```bash
   ./client_app <desired-role|-> <nickname|-> <model|-> [num_games]
   ```
   例:
   - サーバに席決めを任せる手動プレイヤー: `./client_app - Alice manual`
   - X 座を要求するランダム AI: `./client_app X BotX random`
   - ルールベース観戦者: `./client_app spectator Judge rule`
   - ルールベースで10戦連戦: `./client_app X RuleBased rulebased 10`
3. 引数の意味:
   - `desired-role`: `X` / `O` / `spectator` / `-`（サーバ割り当て保持）。
   - `nickname`: 表示名。不要なら `-`。
   - `model`: `manual` または `-` で自動化無効。`random` / `rule` を指定すると組み込みポリシーが有効化されます（追加方法は後述）。
   - `num_games`: （オプション）連戦する回数。省略時は1。自動プレイヤーのみ有効。
4. 実行時の挙動:
   - 接続直後に `ROLE <role> <name> <model>` を1回送信し、サーバが席を確定または却下します。
   - `STATE` ブロックを受信するとローカル描画し、`ongoing` 以外になったタイミングで `[RESULT] ...` を表示します。
   - AutoPlayer が有効な場合、自分のターンを待ってポリシーで手を選び、`MOVE origin,target tile` を自動送信します。

## 3. クライアントに新しい AutoPlayer を追加する

自動化ロジックはすべて `client/main.cpp` にあります。

1. `contrast_ai/` 配下にポリシー実装を追加または include します（例: `contrast_ai/my_policy.hpp`）。既存の `RandomPolicy` / `RuleBasedPolicy` と同じ API（`contrast::Move pick(const contrast::GameState&)`）を満たしてください。
2. `client/main.cpp` 内で `RandomPolicyAdapter` 付近に小さなアダプタークラスを追加し、新ポリシーの `pick(...)` を委譲します。
3. `AutoPlayer::Create` にモデル名トークンを追加し、新アダプターを生成できるようにします。CLI 互換性のため小文字化して比較します。
4. `make client` で再ビルドし、新モデル名を指定して起動（例: `./client_app X TestBot mypolicy`）。スナップショット復元や送信処理などは既存の AutoPlayer 基盤をそのまま利用できます。

## 4. C++ 以外でクライアントを書く場合（プロトコル仕様）

TCP ソケットを開ける言語であれば参加可能です。要点は以下のとおりです。

- **エンドポイント**: サーバホストの TCP ポート `8765` に接続。
- **ラインプロトコル**: すべてのコマンドは ASCII 文字列で、末尾に `\n` を付与します。
- **ハンドシェイク**: `connect()` 直後に必要なら `ROLE <role> <name> <model>\n` を送信（省略可、`-` で未指定）。送らなければサーバ自動割り当てのままです。
- **状態配信**: サーバから次のようなブロックが届きます。
  ```
  INFO You are X (Alice)
  STATE
  turn=X
  status=ongoing
  last=a3,a4 -1
  pieces=a1:X,b2:O,...
  tiles=c3:g,...
  stock_b=X:2,O:1
  stock_g=X:1,O:0
  END
  ```
  各行は `key=value` 形式で、座標は常に2文字（`a1`〜`e5`）。欠損している項目は空マス／在庫ゼロを意味します。
- **指し手送信**: `MOVE <origin>,<target> <tile>\n` を送ります。
  - `<origin>` / `<target>` は `a3` のような座標文字列。
  - `<tile>` は `-1`（タイル無し）または `<coord><color>` で、`color` は `b`（黒タイル）または `g`（灰タイル）。例: `c4b`。
- **状態リクエスト**: いつでも `GET_STATE\n` を送ると最新スナップショットを再送します。
- **エラー処理**: 不正コマンドには `ERROR <text>` が返り、その後ソケットが閉じればセッションも終了します。

この仕様に従えば Python や Rust など任意の言語でボットを実装できます。メッセージ書式は `common/protocol.hpp` の定義と一致させてください。

### Python ランダムボットの例

`client/python_random_bot.py` は上記仕様に従って実装したサンプルです。実行例:

```bash
python3 client/python_random_bot.py --host 127.0.0.1 --port 8765 --role X --name PyBot
```

`--role` を `-` にすればサーバの自動割り当てを利用可能です。タイル所持数と盤面を `STATE` ブロックから復元し、合法手をランダムに選んで `MOVE` を送信します。

### AlphaZero クライアントの使い方

`client/python_alphazero_bot.py` は深層学習ベースの AlphaZero AI クライアントです。

#### 事前準備

1. Python 仮想環境の作成とアクティベート:
   ```bash
   python3 -m venv venv
   source venv/bin/activate
   ```

2. 必要なパッケージのインストール:
   ```bash
   pip install -r client/requirements_alphazero.txt
   ```
   または個別にインストール:
   ```bash
   pip install numpy torch
   ```

3. モデルファイルの確認:
   - デフォルトで `ai/contrast_alphazero/contrast_model_final.pth` を使用
   - カスタムモデルを使う場合は `--model` オプションで指定

#### 実行方法

基本的な起動コマンド:

```bash
# 仮想環境をアクティベート
source venv/bin/activate

# AlphaZero を黒（X）で起動
python3 client/python_alphazero_bot.py --role X --name AlphaZero --simulations 20

# AlphaZero を白（O）で起動
python3 client/python_alphazero_bot.py --role O --name AlphaZero --simulations 20
```

#### オプション

- `--host`: サーバーホスト（デフォルト: 127.0.0.1）
- `--port`: サーバーポート（デフォルト: 8765）
- `--role`: プレイヤー役割 `X`（黒）/ `O`（白）/ `-`（自動割り当て）
- `--name`: 表示名（デフォルト: AlphaZero）
- `--model`: モデルファイルパス（省略時は `ai/contrast_alphazero/contrast_model_final.pth`）
- `--simulations`: MCTS シミュレーション回数（デフォルト: 100、テストには 20 推奨）
- `--games`: 連戦する回数（デフォルト: 1）

#### 連戦実行

1000戦のベンチマークテストを実行する例:

**サーバー起動:**
```bash
./server_app
```

**AlphaZero（黒）vs ルールベース（白）1000戦:**
```bash
# ターミナル1: AlphaZero
source venv/bin/activate
python3 client/python_alphazero_bot.py --role X --name AlphaZero --simulations 100 --games 1000

# ターミナル2: ルールベース
./client_app O RuleBased rulebased 1000
```

**ルールベース（黒）vs AlphaZero（白）1000戦:**
```bash
# ターミナル1: ルールベース
./client_app X RuleBased rulebased 1000

# ターミナル2: AlphaZero
source venv/bin/activate
python3 client/python_alphazero_bot.py --role O --name AlphaZero --simulations 100 --games 1000
```

結果は：
- サーバーコンソールに表示
- `game_results.log` ファイルに記録

#### 対戦例

サーバーとクライアントを別々のターミナルで起動:

**ターミナル1（サーバー）:**
```bash
./server_app
```

**ターミナル2（AlphaZero - 黒）:**
```bash
source venv/bin/activate
python3 client/python_alphazero_bot.py --role X --name AlphaZero --simulations 20
```

**ターミナル3（ルールベース - 白）:**
```bash
./client_app O RuleBased rulebased
```

または逆の組み合わせ:

**ターミナル2（ルールベース - 黒）:**
```bash
./client_app X RuleBased rulebased
```

**ターミナル3（AlphaZero - 白）:**
```bash
source venv/bin/activate
python3 client/python_alphazero_bot.py --role O --name AlphaZero --simulations 20
```

#### 注意事項

- AlphaZero は起動時にモデルの読み込みに数秒かかります
- `--simulations` の値が大きいほど強くなりますが、1手あたりの思考時間も長くなります
  - テスト用: 20
  - 通常対戦: 100
  - 本気対戦: 400-800（高速サーバー推奨）
- 座標系の変換は自動で行われます（プロトコル形式 ↔ AlphaZero 内部形式）
- 詳細な実装情報は `client/ALPHAZERO_README.md` を参照
