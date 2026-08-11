# Security Policy

## 支援版本

安全修正套用於 `master` 與最新正式 x64 release。Windows 7 SP1 至 Windows 11
是目前文件化的執行範圍；無法在每次雲端 CI 自動覆蓋的舊版 Windows，會在發布前
VM checklist 驗證並明確記錄結果。

## 回報漏洞

請使用 GitHub repository 的 **Security → Report a vulnerability** 私密回報功能，
不要在公開 issue 張貼尚未修補的漏洞、憑證、IP 清單或可識別的完整追蹤報告。
回報請包含受影響版本、重現步驟、預期影響與已做的敏感資料遮蔽。

## 權限與網路行為

- 程式 manifest 固定為 `asInvoker`，ICMP trace 不需要系統管理員或封包驅動。
- 程式不執行 shell 命令，也不自動上傳 trace report。
- 啟用公網／節點 metadata 時，ipinfo 是主要 HTTP 提供者；只有單筆主要結果完全
  無法使用才採 Team Cymru 或設定的次要提供者，且不混合不同來源欄位。
- `whoami.ds.akahelp.net` 只用於遞迴 DNS／ECS 區段。Windows 網路設定不列為
  外部資料來源，也不會被本程式修改。
- 所有 provider 有 timeout、總 query budget、固定 concurrency、bounded queue、
  正負 cache、circuit breaker 與 cancellation。TLS 最低為 1.2；不主動降級到
  TLS 1.0／1.1。

## 匯出與供應鏈

HTML／JSON／CSV 皆有對應 escaping；CSV 會防止公式注入。檔案以暫存檔原子替換，
避免磁碟滿或中斷留下半檔。Windows CI 使用唯讀權限、pin action commit、執行
x64 Debug/Release、離線測試、schema/golden 驗證、靜態 CRT/MFC import gate，並
為 release artifact 產生 SHA-256。正式 Authenticode 簽章仍需由受保護的 release
環境與憑證完成，私鑰不得放入 repository 或一般 PR workflow。
