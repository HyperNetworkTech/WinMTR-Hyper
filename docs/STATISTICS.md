# 統計欄位與探測時間語意

本文件定義 WinMTR Hyper schema v1 的量測語意。修改公式或既有欄位意義時，
必須同步調整程式、測試、JSON `statistics` 物件與 schema major version。

## 排程與 timeout

- `interval` 是「同一 TTL 兩個預定發送時刻之間」的間隔，預設 1 秒。
- 一個 interval 內的各 TTL 會均勻錯開，不會同時 burst。
- `timeout` 是每個已成功送出探測的獨立回覆期限，預設 3 秒。它不會把
  發送間隔拖成 3 秒；慢回覆期間，同一 TTL 可同時存在受上限保護的多個探測。
- 排程落後時會跳過已過期的 slot，不會 burst 補發；相關數量與延遲記在
  `scheduler_late_slots`、`scheduler_lateness_total_ms` 與
  `scheduler_lateness_max_ms`。
- 每 TTL 與全 session 都有 in-flight／transport 上限。超過上限的 slot 記為
  `scheduler_skipped`，不算網路丟包。

## 計數器

| 欄位 | 定義 |
|---|---|
| `sent` | 已交給 ICMP transport 的探測數 |
| `completed` | 已在 deadline 內收到可分類結果，或已 timeout 的探測數 |
| `received` | 收到 echo reply、TTL expired 或可用 ICMP path event 的探測數 |
| `timed_out` | deadline 內沒有可用回覆的探測數 |
| `in_flight` | 已送出但尚未到 deadline／完成的探測數 |
| `local_errors` | 本機配置、資源或 API 錯誤；不進入 loss 分母 |
| `scheduler_skipped` | 因 backpressure 略過的 send slot；不算已送出 |
| `cache_skipped` | reply cache 命中而未送出的 slot；不算已送出 |
| `late_completions` | deadline 後才回來、只保留診斷而不改寫統計的 completion |
| `post_destination_completions` | 目的端確認後，高 TTL 已在途 completion；不進正常路徑統計 |

所有累積計數器使用 64 位元無號整數。主表的「已送」對應 `sent`，「已收」
對應 `received`。停止狀態顯示所有 TTL 的實際 `in_flight` 總數。

## Loss

```text
loss_percent = completed == 0 ? 0 : 100 × timed_out / completed
```

`in_flight`、`local_errors`、`scheduler_skipped`、`cache_skipped` 與 late completion
都不計入 loss。這可避免把本機負載、仍在等待或根本未發送的探測誤報為網路丟包。

## RTT、標準差與抖動

- `best_ms`、`average_ms`、`worst_ms`、`last_ms` 僅使用被接受的回覆。
- 無回覆樣本時，UI 留白，JSON 寫 `null`，不以 0 ms 混淆缺值。
- 平均值由 64 位元累積值除以樣本數。
- `stddev_ms` 使用 Welford 線上算法的樣本標準差：樣本數大於 1 時為
  `sqrt(M2 / (N - 1))`；0 或 1 個樣本時為 0。
- `recent_jitter_ms` 是最近兩個已接受 RTT 的絕對差。
- `jitter_ms` 是 RTT 連續差的 1/16 EWMA：
  `J(n) = J(n-1) + (abs(RTT(n)-RTT(n-1)) - J(n-1)) / 16`。
  這是 RTT variation 的平滑值，不宣稱為單向 transit jitter。

## 路徑與 cache

reply cache 預設關閉。啟用後，命中只增加 `cache_skipped`，畫面與 JSON 都可
辨識，使用者不應把它解讀為該 TTL 仍維持每秒實際量測。重設 session、目標、
位址族或會影響封包的追蹤選項時，cache 不跨越新的 data epoch 使用。
