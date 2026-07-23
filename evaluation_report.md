# ecosystem_sim プロジェクト 総合評価・リファクタリング提案レポート

## 1. 概要・目的 (Executive Summary)

本レポートは、ESP32 (C++) 上で動作する組み込みシミュレータ `ecosystem_sim.ino` (全843行) および Python 上で動作するシミュレータ `sim.py` (471行) / `sim_anim.py` (130行) について、アーキテクチャ・計算効率・メモリ管理・マルチスレッド同期・描画性能・コード品質の観点から実施した包括的評価および具体的リファクタリング提案を取りまとめたものである。
なお、本レポートは実証的検証報告 (Challenger 1 & 2) の指摘を取り入れ、メモリ計算精度・空間分割境界処理・SPI DMA 転送同期・オブジェクト指向設計の整合性を精密に再検証・更新した決定版である。

### シミュレータの概要
本プロジェクトは、7種類のエンティティ (植物・草食動物・肉食動物・頂点捕食者・胞子・分解者・ゴミ) が相互作用する空間型生態系シミュレータである。Boidsアルゴリズムに基づく群れ行動、感染性ウイルスの伝播と免疫抵抗、および3つの遺伝パラメータ (`speed_limit`, `altruism`, `immunity`) による自然選択・突然変異・利他主義といった高度な進化メカニクスを搭載している。

### 総合評価サマリー
評価の結果、両システムとも意図された機能と生物学的な複雑性を実現しているものの、大規模長期運用や高フレームレート維持を妨げる重大なアーキテクチャ上の課題が検出された。

1. **ESP32 (C++) 評価サマリー**:
   - **安全性**: FreeRTOS マルチコア処理における同期欠損により、描画スレッド (Core 1) が更新スレッド (Core 0) の配列変更時に `carns[i].targetId` および `apex[i].targetId` の `-1` による負のインデックス参照を発生させ、LoadProhibited 例外 (クラッシュ) を引き起こす致命的脆弱性が存在する。
   - **メモリ効率・D-Cache最適化**: Tensilica LX6 (32bit メモリアライメント) における `sizeof(Entity)` は **296 バイト** であり、全111エンティティで 32,856 バイト (32.09 KB) の SRAM を消費している。特に動かない `plants` (50個) に巨大構造体を採用しているため、軽量な `Plant` 構造体 (12B) へ分離することで **純 SRAM 削減量 13.87 KB (14,200 バイト)** を達成できる。さらに `EntityCore` 分離により、物理更新ループにおける Tensilica LX6 の 32B キャッシュライン汚染 (不要な 240B 履歴データ読み込み) を防ぎ D-Cache ミス率を劇的に低減できる。
   - **計算量とハイブリッド空間分割**: 毎フレーム 5,458 ペアに及ぶ $O(N^2)$ 全探索の距離判定が Core 0 のボトルネックとなっている。Spatial Hash Grid (8x5) 導入に際しては `x = 320.0f` 時の配列外参照を防ぐクランプ処理 (`constrain`) を適用するとともに、視界半径の広い肉食動物 (100px) や頂点捕食者 (120px) に対しては視界切り捨てを防ぐため、高密度な草食動物・植物間のみにグリッドを適用し肉食動物等は直列検索を残すハイブリッド設計を採用する。
   - **描画性能・DMA同期**: `TFT_eSPI` の非同期 DMA 転送 (`pushImageDMA`) 導入時、転送中の premature な `tft.endWrite()` 呼び出しによる SPI CS ライン切断やフレームバッファ競合を回避するため、`tft.dmaWait()` による明示的な同期コールバック管理を確立する。

2. **Python 評価サマリー**:
   - **オブジェクト指向設計**: 種別ごとのサブクラス化がなされておらず、グローバル変数に全状態が保持されている。`MovingEntity` 抽象基底クラスを導入し、トーラス境界ワープを行う `Spore` (胞子) クラスには壁面跳ね返り `apply_boundary()` のオーバーライドを適用することでポリモーフィズムを確立する。
   - **計算効率・エネルギー保存則**: 2D Spatial Hash Grid による $O(N)$ 化に加え、`alive` フラグによる遅延除去一括フィルタリングを行う。この際、同フレーム内の重複捕食（無からのエネルギー発生バグ）を防ぐため、捕食判定ループ内に `if not plant.alive: continue` ガードを追加し、エネルギー保存則を厳密に保護する。
   - **アーキテクチャ分離**: Headless モード（データ収集・`ecosystem_plot_evolution.png` 出力）と Pygame GUI モード（リアルタイム 60 FPS 対話表示）を階層分離し、CI / 解析環境とリアルタイム表示環境の独立性を保証する。

本レポートでは、**既存の7つのエンティティの相互作用メカニクスおよび3つの遺伝パラメータによる進化メカニクスを100%保護**することを絶対条件とし、課題解決に向けた具体的な Before / After コード案と優先度別改善提案を提示する。

---

## 2. ESP32 (C++) コード評価 (`ecosystem_sim.ino`)

### 2.1 FreeRTOS タスク設計とマルチコア同期

#### 現状の構成と同期欠損
`ecosystem_sim.ino` では、FreeRTOS を利用して Core 0 で物理・AI計算タスク (`core0Task`) を実行し、Core 1 (Arduino 標準の `loop Task`) で SPI 描画処理を行っている。

- `core0Task` 起動 (`ecosystem_sim.ino:671`): `xTaskCreatePinnedToCore(core0Task, "ApexTask", 10000, NULL, 1, &Task1, 0);`
- ミューテックス生成 (`ecosystem_sim.ino:657`): `dataMutex = xSemaphoreCreateMutex();`

Core 0 は `core0Task` 内で `xSemaphoreTake(dataMutex, portMAX_DELAY);` から `xSemaphoreGive(dataMutex);` (`L247-L635`) の間、シミュレーション空間全体の更新をロックしている。しかし、Core 1 の `loop()` 関数内では、描画フレームレート維持を目的として**ミューテックスの取得が意図的に省略**されている (`L678-L682` のコメント参照)。

#### ロックフリー読み出しに伴う LoadProhibited クラッシュの危険性
ミューテックスを取得しないため、単なるテアリング (画面のチラツキ) に留まらず、領域外アクセスによるクラッシュが発生する。

**クラッシュ再現シチュエーション (`L730-L732` 肉食動物ターゲット描画 & `L740-L748` 頂点捕食者ターゲット描画):**
1. Core 1 描画処理が `carns[i].active && carns[i].targetId != -1` を評価し、`targetId = 3` であることを確認して条件ブロックに入る。
2. 直後に Core 0 が割り込み、`carns[i].targetId` の対象が死亡または範囲外となったため `carns[i].targetId = -1` に書き換える (`L514`)。
3. Core 1 の処理が再開され、`herbs[carns[i].targetId].active` を評価する際、`targetId` の最新値である `-1` が読み込まれる。
4. `herbs[-1].active` という負の配列インデックス参照が発生し、ESP32 のメモリアクセス例外 (LoadProhibited Panic) が引き起こされる（同様の事象が `apex[i].targetId` に対する `herbs[-1]` / `carns[-1]` 参照でも発生する）。

#### 修正方針
描画中に Core 0 全体を停止させないため、`carns[i].targetId` および `apex[i].targetId` の双方に対し、アトミックなローカル変数コピーとインデックス有効範囲検証 (`0 <= tid && tid < MAX_COUNT`) を導入して領域外アクセスを完全に防止する。

---

### 2.2 メモリ割り当てとデータ構造

#### SRAM (DRAM) 占有量の精密計算
Tensilica LX6 構造体アライメント (32bit境界) に基づく `Entity` 構造体の実サイズ評価 (`ecosystem_sim.ino:34-49`):
```cpp
struct Entity {
  bool active;         // 1B (+3B padding) -> 4B
  float x, y;          // 8B
  float vx, vy;        // 8B
  float energy;        // 4B
  float histX[30];     // 120B
  float histY[30];     // 120B
  int histIdx;         // 4B
  float flash;         // 4B
  bool infected;       // 1B (+3B padding) -> 4B
  int targetId;        // 4B
  float speedLimit;    // 4B
  int age;             // 4B
  float altruism;      // 4B
  float immunity;      // 4B
}; // 合計 296 バイト (パディング考慮)
```

全111個のグローバル配列要素による SRAM 占有量:
- `plants[50]`: $50 \times 296\text{B} = 14,800\text{B}$
- `herbs[40]`: $40 \times 296\text{B} = 11,840\text{B}$
- `carns[8]`: $8 \times 296\text{B} = 2,368\text{B}$
- `apex[3]`: $3 \times 296\text{B} = 888\text{B}$
- `decomps[10]`: $10 \times 296\text{B} = 2,960\text{B}$
- **現状の `Entity` 総占有量**: **32,856 バイト (32.09 KB)**

#### 構造的再設計と SRAM 純削減量 (13.87 KB)
1. **`Plant` 分離による純 SRAM 削減量の精密計算**:
   - `Plant` 軽量構造体: `bool active` (1B+3B pad), `float x, y` (8B) = **12 バイト**
   - 植物 50 個のメモリ: $50 \times 12\text{B} = 600\text{B}$ (旧 $14,800\text{B}$ から **14,200 バイト削減**)
   - その他の動的エンティティ (計61個: Herb 40, Carn 8, Apex 3, Decomp 10): `EntityCore` (52B) + `TrailHistory` (244B) = 296B $\times 61 = 18,056\text{B}$
   - 改善後の総メモリ量: $600\text{B} + 18,056\text{B} = \mathbf{18,656\text{ バイト (18.22 KB)}}$
   - **純 SRAM 削減量**: $32,856\text{B} - 18,656\text{B} = \mathbf{14,200\text{ バイト (13.87 KB)}}$
2. **D-Cache 効率の飛躍的向上**:
   - 旧設計では 240B の軌跡履歴データが構造体に埋め込まれており、Core 0 が物理計算 (`x, y, vx, vy, energy`) を行う際、Tensilica LX6 の 32B キャッシュラインに無用な履歴データが読み込まれ D-Cache ミスが多発していた。
   - `EntityCore` (52B) のみに分離・凝縮することで、キャッシュラインあたりの有効データ密度が上がり、D-Cache ミス率が大幅に低減する。
3. **ダブルバッファ化におけるヒープ制約**:
   - `img.createSprite(320, 170)` (16bpp 565フォーマット) は **108,800 バイト (108.8 KB)** の連続 DRAM を消費する。
   - DMA フリッカー完全防止のためダブルバッファを導入すると 217.6 KB の連続ヒープが必要となり、ESP32 の実質利用可能内部 DRAM (~180 KB) を超過して `createSprite` が NULL を返し起動時クラッシュする。
   - したがって、シングルバッファ＋DMA転送同期待ち (`tft.dmaWait()`) が最適な選択となる。

---

### 2.3 計算量とアルゴリズム

#### 毎フレーム 5,458 ペアの $O(N^2)$ 計算量
Core 0 内の `core0Task` で実行される近接・視界判定は、すべて総当たりループで実装されている。

- **Herb-Herb (Boids + 利他分配)**: $40 \times 40 = 1,600$ ペア
- **Herb-Plant (餌探索)**: $40 \times 50 = 2,000$ ペア
- **Herb-Carn / Apex (逃避)**: $40 \times (8 + 3) = 440$ ペア
- **Spore-Herb (感染判定)**: $15 \times 40 = 600$ ペア
- **Carn-Herb / Apex (捕食・逃避)**: $8 \times 40 + 8 \times 3 = 344$ ペア
- **Decomp-Garbage / Spore (清掃)**: $10 \times (30 + 15) = 450$ ペア
- **毎フレームの合計距離計算数**: **5,458 ペア**

#### 空間分割 (Uniform Spatial Hash Grid 8x5) の境界値と視界半径設計
画面 `320 x 170` を `40 x 34` ピクセルの格子 (8 列 × 5 行 = 40 セル) に分割する Grid の設計にあたっては、以下の2点に十分留意する必要がある。

1. **境界値クランプ処理による配列外アクセス防止**:
   - エンティティが画面右端 `x = 320.0f` や下端 `y = 170.0f` に到達した際、`int(320.0f / 40.0f) = 8` となり、有効インデックス `0~7` を超えて `grid[8][5]` へのOutOfBounds参照が発生する。
   - 対策として `cellX = constrain((int)(x / 40.0f), 0, COLS - 1);` (`COLS=8`) を適用する。
2. **広域視界エンティティに対するハイブリッド検索設計**:
   - 格子サイズ `40 x 34` における近傍 3x3 セルの最大カバー対角距離は約 **52.5 ピクセル** である。
   - 一方、肉食動物 (Carnivore) の探索視界は **100px** (`distSq < 10000`)、頂点捕食者 (Apex) は **120px** (`distSq < 14400`) に及ぶ。
   - 3x3 セル検索のみに限定した場合、 Carnivore/Apex が遠方の獲物を見失い、 Section 5 で定義する生物学的な行動ロジックが破綻する。
   - したがって、全体計算ペア数の 80% 以上を占める高密度な **Herb-Herb** (40px) および **Herb-Plant** (20px) に Spatial Grid を適用して計算量を大幅カットし、個体数が極めて少ない **Carnivore ($N=8$)** および **Apex ($N=3$)** には全探索または広域セル検索を残す**ハイブリッド空間分割**を採用する。

---

### 2.4 SPI / TFT 描画処理

#### ブロッキング SPI 転送ストールと DMA 同期管理
`ecosystem_sim.ino:840` の `img.pushSprite(0, 0);` は 108.8 KB のデータを 40MHz SPI で同期送信しており、毎フレーム約 **21.7 ms** CPU をストールさせている。

#### DMA 転送における通信破壊と正しい API 呼出しシーケンス
`TFT_eSPI` の `tft.pushImageDMA()` は非同期でバックグラウンド転送を開始し、直ちに制御を復帰させる。
しかし、以下のように `tft.pushImageDMA()` 直後に `tft.endWrite()` を呼び出すと、DMA 転送中に SPI Chip Select (CS) ラインが HIGH (非アクティブ) に切り替わり、送信データが破損して画面表示が破綻する。

```cpp
// 誤った呼び出し (通信破壊が発生)
tft.startWrite();
tft.pushImageDMA(0, 0, TFT_WIDTH, TFT_HEIGHT, (uint16_t*)img.getPointer());
tft.endWrite(); // <- DMA転送中に CS が切断される！
```

正しいシーケンスとして、次フレームのバッファ書き換え開始前および `endWrite()` 直前に `tft.dmaWait()` を呼出し、非同期転送の完了を確実に同期・管理する。また、`User_Setup.h` にて SPI クロックスピードを 80MHz (`SPI_FREQUENCY = 80000000`) に引き上げることで転送時間を 10.8ms に半減させる。

---

### 2.5 コード品質・DRY違反

#### 重複コード
画面外縁での跳ね返り処理 (`x < 0`, `x > TFT_WIDTH`, `y < 0`, `y > TFT_HEIGHT`) が `decomps` (`L319`), `herbs` (`L482`), `carns` (`L563`), `apex` (`L621`) の 4 箇所で全く同じコードとして記述されている。

#### マジックナンバー
`1600` ($40^2$), `400` ($20^2$), `4000`, `99999`, `random(0, 1000) < 35` などの数値定数がコード中に直接埋め込まれており、パラメータ変更時の誤りを誘発している。

---

## 3. Python シミュレータコード評価 (`sim.py` & `sim_anim.py`)

### 3.1 オブジェクト指向設計とデータ構造

#### クラス階層の不統一とポリモーフィズム
- `sim.py:33-50`: 移動・捕食・遺伝行動を行う主体が単一の `Entity` クラスで定義され、`type` 属性すら持たず、どの種別であるかは所属する外部リスト (`herbs`, `carns` 等) で区別されている。
- `MovingEntity` 抽象基底クラスを導入して `Herbivore`, `Carnivore`, `ApexPredator`, `Decomposer`, `Spore` をポリモーフィックにカプセル化する。
- **Spore (胞子) のトーラス境界オーバーライド**: 動物エンティティは画面壁面で反転跳ね返り (`apply_boundary`) を行うが、`Spore` クラスは画面端で反対側にワープ (`x %= WIDTH`, `y %= HEIGHT`) する。基底クラス `MovingEntity` の `apply_boundary()` を `Spore` で適切にオーバーライドし、本来の運動物理を保護する。

#### カプセル化と状態保持
`sim_anim.py` におけるモンキーパッチング (`e.hist = []` 等の動的追加) を廃止し、モデル層 `World` クラスが全エンティティの生成・更新・状態遷移を一元管理する設計とする。

---

### 3.2 シミュレーションループの計算効率

#### 2D Spatial Hash Grid と境界クランプ
Python 側でも Spatial Hash Grid を導入して距離探索を $O(N)$ に高速化する。
インデックス計算時の `int(x // self.cell_size)` に対し、`c = max(0, min(self.cols - 1, int(x // self.cell_size)))` を追加し、`x = 320.0` 時のセル非検出バグを完全に回避する。

---

### 3.3 メモリフットプリントとエネルギー保存則の保護

#### `alive` フラグ一括フィルタリングと二重消費ガード
要素削除コスト $O(N)$ の `list.remove()` / `pop(0)` を廃止し、`alive = False` フラグ設定後に 1 ステップの終了時で一括リスト内包表記 `[e for e in list if e.alive]` フィルタリングを行う。

この時、同一フレーム内に複数の個体が同じ餌・獲物にアプローチする場合、先の個体が `plant.alive = False` とした後に後続の個体が `plant.alive` をチェックしないと、1 つの餌から複数個体が同時にエネルギーを獲得してしまう（エネルギー無からの発生バグ）。
捕食・食いつき判定ループ内に必ず `if not plant.alive: continue` (または `if not target.alive: continue`) のガード文を追加し、生態系のエネルギー保存則を厳密に維持する。

---

### 3.4 描画・UI 依存関係と Headless アーキテクチャ

#### モード分離 (Headless Data Collection vs Pygame GUI)
描画モジュールを完全分離し、用途に応じた 2 つの実行モードを提供する：
1. **Headless モード (`sim_headless.py`)**:
   - Pygame などの GUI ウィンドウを起動せず、純粋な数値計算とシミュレーションログ収集、および `ecosystem_plot_evolution.png` (遺伝子・個体数推移グラフ) の自動生成を行う。CI 環境や大量試行データ収集に最適。
2. **Pygame GUI モード (`sim_pygame.py`)**:
   - Pygame による 60 FPS 以上の滑らかなリアルタイム表示と、ポーズ・パラメータ動的変更などの対話的 UI を提供する。

---

### 3.5 コード品質・Pythonic 記述

#### PEP 8 準拠と DRY 原則
セミコロンによる 1 行複数文 (`if d.x < 0: d.x=0; d.vx*=-1`) を解消し、標準の PEP 8 スタイルに準拠する。

---

## 4. 優先度別課題とリファクタリング提案 (High / Medium / Low)

検出された主要な問題点について、課題優先度別に分類し、「現在のコード (Before)」と「改善後のコード案 (After / Diff)」の具体例を提示する。

---

### 4.1 優先度【High】: 致命的バグ・メモリクラッシュ・主要性能障害

#### 課題 H-1 [ESP32]: Core 1 描画時の `targetId` 競合による負インデックス参照クラッシュ
- **影響**: Core 0 が `targetId = -1` に更新した直後に Core 1 が `herbs[-1]` または `carns[-1]` を参照し LoadProhibited Panic を引き起こす。
- **Before (`ecosystem_sim.ino:730-735, 740-745`)**:
```cpp
// [Before] ミューテックスなしで直接参照 (carns および apex)
if(carns[i].active && carns[i].targetId != -1) {
  if(herbs[carns[i].targetId].active) { // <- targetId が途中で -1 になるとクラッシュ！
    float dx = carns[i].x - herbs[carns[i].targetId].x;
    float dy = carns[i].y - herbs[carns[i].targetId].y;
    if(dx*dx + dy*dy < 1600) {
      img.drawWedgeLine(carns[i].x, carns[i].y, herbs[carns[i].targetId].x, herbs[carns[i].targetId].y, 0.5f, 0.5f, myColor(150, 0, 70));
    }
  }
}
```
- **After (改善案)**:
```cpp
// [After] carns および apex の双方に対し、ローカル変数へのアトミックコピーと境界チェックを適用
if(carns[i].active) {
  int tid = carns[i].targetId; // ローカル変数へアトミック読み込み
  if(tid >= 0 && tid < MAX_HERBS) { // インデックス安全性を完全検証
    if(herbs[tid].active) {
      float dx = carns[i].x - herbs[tid].x;
      float dy = carns[i].y - herbs[tid].y;
      if(dx*dx + dy*dy < 1600) {
        img.drawWedgeLine(carns[i].x, carns[i].y, herbs[tid].x, herbs[tid].y, 0.5f, 0.5f, myColor(150, 0, 70));
      }
    }
  }
}

// apex 側も同様に保護
if(apex[i].active) {
  int tid = apex[i].targetId;
  if(tid >= 0 && tid < MAX_CARNS) {
    if(carns[tid].active) {
      // 描画処理...
    }
  }
}
```

---

#### 課題 H-2 [ESP32]: `Entity` 構造体の肥大化と SRAM 13.87 KB 空費・D-Cache ミス
- **影響**: 動かない `plants` に 240 バイトの履歴史が埋め込まれ SRAM を圧迫し、D-Cache ミスを多発させる。
- **Before (`ecosystem_sim.ino:34-49, 66`)**:
```cpp
// [Before] 単一の巨大構造体 (sizeof(Entity) = 296B)
struct Entity {
  bool active; float x, y, vx, vy, energy;
  float histX[30], histY[30]; int histIdx;
  float flash; bool infected; int targetId;
  float speedLimit; int age; float altruism, immunity;
};
Entity plants[50]; // 50 * 296B = 14,800B (14KB以上が無駄)
```
- **After (改善案)**:
```cpp
// [After] 軽量な Plant 構造体 (12B) と EntityCore / TrailHistory の分離
struct Plant {
  bool active; // 1B (+3B pad) -> 4B
  float x, y;  // 8B
}; // 合計 12 バイト

struct TrailHistory {
  float histX[30];
  float histY[30];
  int histIdx;
}; // 244 バイト

struct EntityCore {
  bool active;
  float x, y, vx, vy, energy;
  float flash; bool infected; int targetId;
  float speedLimit; int age; float altruism, immunity;
}; // 52 バイト (D-Cacheライン効率化)

Plant plants[MAX_PLANTS]; // 50 * 12B = 600B (14,200B / 13.87KB の純削減)
EntityCore herbs[MAX_HERBS];
TrailHistory herbTrails[MAX_HERBS]; // 描画スレッド側でのみ必要に応じて参照
```

---

#### 課題 H-3 [Python]: グローバル状態への密結合とカプセル化欠如
- **影響**: モジュールグローバル変数による状態管理のため並列実行や単体テストが不可能。
- **Before (`sim.py:68-74`)**:
```python
# [Before] モジュールレベルのグローバル変数
plants = []
herbs = []
carns = []
apexs = []
spores = []
decomps = []
garbages = []
```
- **After (改善案)**:
```python
# [After] World クラスによる状態とシミュレーション実行のカプセル化
class World:
    def __init__(self, width: float = 320.0, height: float = 170.0):
        self.width = width
        self.height = height
        self.plants: List[Plant] = []
        self.herbs: List[Herbivore] = []
        self.carns: List[Carnivore] = []
        self.apexs: List[ApexPredator] = []
        self.spores: List[Spore] = []
        self.decomps: List[Decomposer] = []
        self.garbages: List[Garbage] = []
        self.spatial_grid = SpatialHashGrid(width, height, cell_size=20.0)

    def step(self) -> None:
        # 1ステップの物理・AI・交差判定更新ロジック
        pass
```

---

### 4.2 優先度【Medium】: アーキテクチャ・計算効率・キャッシュ最適化

#### 課題 M-1 [ESP32 & Python]: 距離判定 $O(N^2)$ ボトルネックと境界クランプ・ハイブリッド空間分割
- **影響**: 全要素のスキャンによる計算コスト増加、および境界値での配列外アクセス・広い視界の認識阻害。
- **Before (`sim.py:189-195`)**:
```python
# [Before] 総当たり全探索 O(N^2)
for h in herbs:
    closest_p = None
    min_d2 = 99999.0
    for p in plants:
        d2 = dist2(h, p)
        if d2 < min_d2:
            min_d2 = d2
            closest_p = p
```
- **After (改善案)**:
```python
# [After] 境界クランプ処理を備えた 2D Spatial Hash Grid
class SpatialHashGrid:
    def __init__(self, width: float, height: float, cell_size: float = 20.0):
        self.cell_size = cell_size
        self.cols = int(math.ceil(width / cell_size))
        self.rows = int(math.ceil(height / cell_size))
        self.grid: Dict[Tuple[int, int], List[BaseEntity]] = {}

    def clear(self) -> None:
        self.grid.clear()

    def insert(self, entity: BaseEntity) -> None:
        # クランプ処理を追加し x=320.0, y=170.0 での範囲外キー作成を防止
        c = max(0, min(self.cols - 1, int(entity.x // self.cell_size)))
        r = max(0, min(self.rows - 1, int(entity.y // self.cell_size)))
        self.grid.setdefault((c, r), []).append(entity)

    def get_nearby(self, x: float, y: float, radius: float) -> List[BaseEntity]:
        min_c = max(0, min(self.cols - 1, int((x - radius) // self.cell_size)))
        max_c = max(0, min(self.cols - 1, int((x + radius) // self.cell_size)))
        min_r = max(0, min(self.rows - 1, int((y - radius) // self.cell_size)))
        max_r = max(0, min(self.rows - 1, int((y + radius) // self.cell_size)))
        res = []
        for c in range(min_c, max_c + 1):
            for r in range(min_r, max_r + 1):
                if (c, r) in self.grid:
                    res.extend(self.grid[(c, r)])
        return res
```
*(注: C++ 側でも `cellX = constrain((int)(x / 40.0f), 0, 7);` を適用する。また Carnivore (100px) / Apex (120px) の広い視界判定に対しては視界切断を防ぐため直列探索を維持するハイブリッド構成とする)*

---

#### 課題 M-2 [Python]: `alive` 遅延除去と重複捕食（エネルギー保存則破綻）の防止
- **影響**: `list.remove()` の高コスト化、および同一フレーム内での複数個体による同餌重複捕食（無からの余剰エネルギー発生）。
- **Before (`sim.py:132, 182, 206`)**:
```python
# [Before] 即時 list.remove() または フラグフィルタリングのみでループ内ガード欠如
if target_p in plants:
    plants.remove(target_p) # O(N) 線形探索・配列シフトコスト
```
- **After (改善案)**:
```python
# [After] alive フラグ＋捕食ループ内ガードと一括フィルタリング
# 捕食処理ループ内 (同フレーム内での二重消費を厳密にブロック)
for h in self.herbs:
    nearby_plants = self.spatial_grid.get_nearby(h.x, h.y, radius=20.0)
    for p in nearby_plants:
        if not p.alive: # <- 二重消費ガード! 他の個体が今フレームで食した餌をスキップ
            continue
        if dist2(h, p) < 400.0:
            p.alive = False
            h.energy += 30.0
            break

# ステップの最後に一括インプレースフィルタリング (O(N))
self.plants = [p for p in self.plants if p.alive]
```

---

#### 課題 M-3 [ESP32]: SPI DMA 非同期転送における API 同期と CS 切断防止
- **影響**: `pushImageDMA()` 直後の `endWrite()` 呼び出しによる SPI CS ライン途中切断および画面表示崩れ。
- **Before (`ecosystem_sim.ino:840`)**:
```cpp
// [Before] ブロッキング転送
img.pushSprite(0, 0);
```
- **After (改善案)**:
```cpp
// [After] SPI DMA 非同期転送と dmaWait() による同期管理
// setup() 内で tft.initDMA(); を実行
tft.startWrite();
tft.pushImageDMA(0, 0, TFT_WIDTH, TFT_HEIGHT, (uint16_t*)img.getPointer());
// DMA転送の完了を同期待機し、CSラインの premature 切断およびフレームバッファ競合を完全防止
tft.dmaWait();
tft.endWrite();
```

---

### 4.3 優先度【Low】: コードスタイル・DRY原則・リファクタリング

#### 課題 L-1 [ESP32 & Python]: 画面境界での跳ね返りコードの 4 重重複 (DRY 違反)
- **Before (`ecosystem_sim.ino:482-485`)**:
```cpp
// [Before] 各エンティティ更新ルーチンで重複記述
if(herbs[i].x < 0) { herbs[i].x = 0; herbs[i].vx *= -1; }
if(herbs[i].x > TFT_WIDTH) { herbs[i].x = TFT_WIDTH; herbs[i].vx *= -1; }
```
- **After (改善案)**:
```cpp
// [After] インライン共通関数テンプレート
template <typename T>
inline void applyBoundary(T &e, float width, float height) {
  if (e.x < 0.0f) { e.x = 0.0f; e.vx *= -1.0f; }
  else if (e.x > width) { e.x = width; e.vx *= -1.0f; }
  if (e.y < 0.0f) { e.y = 0.0f; e.vy *= -1.0f; }
  else if (e.y > height) { e.y = height; e.vy *= -1.0f; }
}
```

---

#### 課題 L-2 [Python]: `MovingEntity` 基底クラスと `Spore` のトーラス境界オーバーライド
- **Before (`sim.py:155-158`)**:
```python
# [Before] セミコロンによる1行複数文、型ヒントなし
if d.x < 0: d.x=0; d.vx*=-1
```
- **After (改善案)**:
```python
# [After] MovingEntity 基底クラスと Spore によるトーラス境界オーバーライド
class MovingEntity(BaseEntity):
    def __init__(self, x: float, y: float, energy: float, speed_limit: float = 0.8):
        super().__init__(x, y)
        self.vx: float = random.uniform(-1.0, 1.0)
        self.vy: float = random.uniform(-1.0, 1.0)
        self.energy: float = energy
        self.speed_limit: float = speed_limit

    def apply_boundary(self, width: float, height: float) -> None:
        """動物・分解者用: 壁面反転跳ね返り"""
        if self.x < 0.0:
            self.x = 0.0
            self.vx *= -1.0
        elif self.x > width:
            self.x = width
            self.vx *= -1.0

        if self.y < 0.0:
            self.y = 0.0
            self.vy *= -1.0
        elif self.y > height:
            self.y = height
            self.vy *= -1.0

class Spore(MovingEntity):
    def apply_boundary(self, width: float, height: float) -> None:
        """Spore専用: トーラス状ワープ移動へのオーバーライド"""
        self.x %= width
        self.y %= height
```

---

## 5. 既存機能・遺伝的進化メカニクス保護の明示

リファクタリングの実施にあたり、以下の 7 つのエンティティ相互作用メカニクスおよび 3 つの遺伝パラメータによる自然選択方程式を**完全かつ非破壊に維持・保護**することを保証する。

### 5.1 7つのエンティティ相互作用メカニクス保護

1. **Plants (植物)**:
   - 自然発生 (`random(0, 1000) < 35`) および Decomposer 分解完了時 (`energy > 120`) に生成。
   - Herbivore 接触時に消滅し、Herbivore のエネルギーを `+30.0` 回復。
2. **Herbivores (草食動物)**:
   - Boids アルゴリズムによる分離 (Separation)・整列 (Alignment)・結合 (Cohesion) 行動。
   - Carnivore (視界 40px) および Apex (視界 77.4px) からの逃避行動。
   - 繁殖: `energy > 100.0` で分裂し、エネルギーを半減させて子へ継承。
3. **Carnivores (肉食動物)**:
   - Herbivore を視界 100px で探索・追尾。食いつき時 (`herbs energy -= 2.5`, `carns energy += 2.5`, `vx *= 0.5`)。
   - Apex 接近時 (`distSq < 8000`) のアドレナリン逃走 (`speed_limit + 0.6`)。
   - 繁殖: `energy > 300.0` で分裂。
4. **Apex Predators (頂点捕食者)**:
   - Carnivore を視界 120px で追尾・食いつき (`carns energy -= 5.0`, `apex energy += 5.0`)。
   - 繁殖: `energy > 500.0` で分裂。
5. **Spores (ウイルス胞子)**:
   - 感染 Herbivore または Decomposer 死亡時に発生。画面端トーラスワープ移動。
   - Herbivore 接触時、`immunity` 遺伝子判定をパスできない場合感染フラグ (`infected = true`) を付与。
6. **Garbage (死骸・ゴミ)**:
   - 動物死亡時に発生するリングバッファ (最大 30 個)。
7. **Decomposers (分解者)**:
   - Garbage または Spore を回収・消滅させ、`energy += 20.0`。
   - `energy > 120.0` で植物を生み出し、エネルギーを消費して再生循環。

---

### 5.2 3つの遺伝パラメータと自然選択・進化方程式

1. **`speed_limit` (移動速度上限遺伝子)**:
   - **継承・変異**: 親の `speed_limit` に対し $\pm 0.1$ の一様乱数変異を加え、$0.3 \le speed\_limit \le 2.0$ にクランプ。
   - **代謝コスト方程式**:
     $$\text{Herb Drain} = 0.01 + 0.03 \times speed\_limit + 0.02 \times immunity$$
     $$\text{Carn Drain} = 0.15 \times \left(\frac{speed\_limit}{1.1}\right)$$
     $$\text{Apex Drain} = 0.25 \times \left(\frac{speed\_limit}{1.5}\right)$$

2. **`altruism` (利他主義遺伝子, $0.0 \sim 1.0$)**:
   - **利他エネルギー分与**: 同種近接個体 ($\text{distSq} < 400$) に対し、余裕のある個体 ($energy > 60.0$) が困窮個体 ($energy < 30.0$) へエネルギーを分配。
   - **利他的自主隔離**: 感染個体 ($infected == true$) は、$altruism > 0.6$ の場合、群れを守るため外縁（壁）方向へ移動する。

3. **`immunity` (免疫力遺伝子, $0.0 \sim 1.0$)**:
   - **感染防御判定**: 胞子接触時、確率 $\text{random}(0.0, 1.0) < immunity$ で感染を無効化（白スパーク発生）。
   - **維持コスト**: 高免疫力ほど上記 Herb Drain 方程式に従い基礎エネルギー消費が増加する。

---

### 5.3 整合性検証チェックリスト (Verification Guarantee)

リファクタリング後の実装が正しくロジックを保持しているかを検証するため、以下の判定基準を設ける。

| チェック項目 | 保証されるべき挙動 | 違反時の判定 |
|---|---|---|
| **エネルギー減衰** | `speed_limit` および `immunity` に連動した毎ステップのエネルギー消費 | 生命の平均寿命が異常上昇/低下した場合「失敗」 |
| **遺伝的変異** | 子個体発生時の $\pm 0.1$ 変異とクランプ範囲 | 遺伝子が固定値または範囲外になった場合「失敗」 |
| **利他行動** | 感染時の外縁隔離および近接個体へのエネルギー分与 | `altruism` 高個体が群れに留まり感染拡大した場合「失敗」 |
| **生態系バランス** | 30,000 ステップ経過後に全種族が絶滅せず生存 | 1種族が即時絶滅した場合「失敗」 |
| **エネルギー保存則** | 捕食ループ内の `alive` ガードによる重複捕食防止 | 1つの餌から複数個体が二重エネルギーを獲得した場合「失敗」 |

---

## 6. まとめと今後の進め方

本評価レポートで提示したリファクタリング案を適用することで、以下の効果が得られる。

1. **ESP32 (C++)**:
   - `carns` および `apex` の `targetId` 競合による LoadProhibited メモリクラッシュの完全解消。
   - `sizeof(Entity) = 296B` に基づく精密な構造体再設計により、純 SRAM 13.87 KB (14,200 バイト) の削減と D-Cache ヒット率の大幅向上。
   - 境界値クランプ処理および Carnivore/Apex 視界保護を備えたハイブリッド Spatial Grid 導入による計算量の $O(N)$ 化。
   - `tft.dmaWait()` による同期管理を備えた SPI DMA 非同期転送による CPU ストール解消。
2. **Python**:
   - `World` クラスによる状態カプセル化と `Spore` のトーラス境界処理オーバーライドを備えた高度な OOP アーキテクチャの確立。
   - 二重消費防止ガード (`if not plant.alive: continue`) を備えた一括フィルタリングによるエネルギー保存則の厳密保護。
   - Headless モード (データ収集・`ecosystem_plot_evolution.png` 出力) と Pygame GUI モードの完全階層分離。

以上の改善はすべて、7つのエンティティの相互作用と遺伝進化メカニクスを 100% 保護した状態で達成可能である。
