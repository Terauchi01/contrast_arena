# AlphaZero Client Setup and Usage

## 必要な環境

AlphaZeroクライアントを使用するには、以下のパッケージが必要です：

- Python 3.8以上
- numpy
- torch (PyTorch)

## インストール方法

### 方法1: uv環境を使用（推奨）

```bash
cd ai/contrast_alphazero
uv sync  # 依存パッケージをインストール
```

### 方法2: システムPythonを使用

```bash
pip3 install numpy torch
```

### 方法3: venv環境を作成

```bash
python3 -m venv venv
source venv/bin/activate
pip install numpy torch
```

## 使用方法

### サーバーを起動

```bash
./server_app
```

### AlphaZeroクライアントを起動

#### uv環境の場合:

```bash
cd client
uv run --directory ../ai/contrast_alphazero python python_alphazero_bot.py --role X --simulations 50
```

#### venv環境の場合:

```bash
source venv/bin/activate
python3 client/python_alphazero_bot.py --role X --simulations 50
```

### オプション

- `--host`: サーバーホスト (デフォルト: 127.0.0.1)
- `--port`: サーバーポート (デフォルト: 8765)
- `--role`: プレイヤーロール X または O (デフォルト: X)
- `--name`: プレイヤー名 (デフォルト: AlphaZero)
- `--model`: モデルファイルパス (デフォルト: ai/contrast_alphazero/contrast_model_final.pth)
- `--simulations`: MCTSシミュレーション回数 (デフォルト: 100)

### 対戦例

AlphaZero vs Random:

```bash
# ターミナル1: サーバー起動
./server_app

# ターミナル2: AlphaZero (X)
python3 client/python_alphazero_bot.py --role X --simulations 50

# ターミナル3: Random (O)
./client_app O random_player random
```

## 座標系について

AlphaZeroの内部座標とプロトコル座標は以下のように変換されます：

- **AlphaZero**: P1は y=4 (配列下部), P2は y=0 (配列上部)
- **プロトコル**: X(P1)は y=0 (ボード上), O(P2)は y=4 (ボード下)

クライアントが自動的に座標変換を行います。

## トラブルシューティング

### "No module named 'numpy'" エラー

```bash
pip3 install numpy torch
```

### "Model file not found" 警告

モデルファイルが存在しない場合は未学習モデルで動作します。
学習済みモデルを使用する場合は `--model` オプションでパスを指定してください。

### シミュレーション回数の調整

- 強さを上げる: `--simulations 200` (遅くなる)
- 速度を上げる: `--simulations 10` (弱くなる)
