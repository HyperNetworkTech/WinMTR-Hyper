# 命令列介面

```text
WinMTR.exe [選項] ["目標主機"]
```

有目標主機時會自動開始追蹤；沒有目標時開啟一般 GUI。所有數值都採嚴格解析，
不接受尾隨字元、NaN、Infinity 或超出範圍的值。命令列只覆蓋本次 session，
不會在背景寫回 Registry；使用者之後在「選項」按下確定才會明確保存設定。

## 數值選項

| 選項 | 範圍／單位 | 預設 |
|---|---|---|
| `--interval VALUE`、`-i` | 0.1–60 秒 | 1 秒 |
| `--size VALUE`、`-s` | 0–4096 bytes | 64 |
| `--maxLRU VALUE`、`-m` | 1–256 | 128 |
| `--max-hops VALUE` | 1–64 TTL | 30 |
| `--timeout VALUE` | 100–10000 ms | 3000 |
| `--cycles VALUE` | 0–100000；0 表示持續 | 0 |
| `--tos VALUE` | 0–255 raw ToS/DS byte | 0 |
| `--pattern VALUE` | 0–255，或 `random` | 32 |
| `--start-ttl VALUE` | 1–64，且不得大於 max hops | 1 |
| `--minimum-ttl VALUE` | 0–64，且不得大於 max hops | 0 |
| `--unknown-hosts VALUE` | 1–64 個連續未知 TTL | 12 |
| `--ecmp-limit VALUE` | 畫面顯示 1–128 responders | 8 |
| `--cache VALUE` | 0–86400 秒；0 關閉 | 0 |
| `--public-refresh-minutes VALUE` | 1–1440 分鐘，切成固定週期刷新 | 30 |

## 開關選項

| 選項 | 行為 |
|---|---|
| `--numeric`、`-n` | 不做反向 DNS |
| `--resolve` | 啟用反向 DNS |
| `--lookup-asn` / `--no-lookup-asn` | 啟用／關閉節點 ASN 與業者查詢 |
| `--dont-fragment` / `--allow-fragment` | 設定／取消 IPv4 DF |
| `--ipv4-only` | 只使用 IPv4 |
| `--ipv6-only` | 只使用 IPv6 |
| `--dual-stack` | 同時允許 IPv4 與 IPv6 |
| `--public-info` / `--no-public-info` | 啟用／關閉目前公網資訊查詢 |
| `--public-refresh-on-change` | 網路介面變更後刷新公網資訊 |
| `--help`、`-h` | 顯示 GUI 說明 |

## 結束碼

| 代碼 | 意義 |
|---|---|
| 0 | 正常啟動／關閉，或使用者要求 help |
| 2 | 未知選項、缺值、重複目標、數值格式或範圍錯誤 |

解析錯誤同時寫入繼承的標準錯誤輸出並顯示 GUI 訊息，方便終端與桌面使用者。
