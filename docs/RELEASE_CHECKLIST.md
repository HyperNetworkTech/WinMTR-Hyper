# 發布檢查清單

## CI 自動 gate

- checked-in Visual Studio solution 與 authoritative CMake graph 都完成 x64 Debug／Release。
- 所有離線 core、provider／CLI fuzz corpus、IPv4／IPv6 loopback、schema/golden 測試通過。
- Release 使用靜態 CRT/MFC、manifest 維持 `asInvoker`，且沒有靜態匯入只在
  Windows 8+ 才能使用的 `GetAddrInfoExCancel`／`GetAddrInfoExOverlappedResult`。
- 產出 `WinMTR.exe`、PDB、`SHA256SUMS.txt`、完整 `IMPORTS.txt`、JSON schema 與
  SPDX 2.3 `WinMTR-Hyper.spdx.json`，每個 artifact 都能追溯到 git commit。
- scheduled／手動 workflow 的 AddressSanitizer core tests 通過。

## 受保護 release 環境 gate

- 使用 HSM／受保護 CI secret 中的正式憑證簽署 `WinMTR.exe`；私鑰不得進入
  repository、PR workflow、log 或 artifact。
- 執行 `signtool verify /pa /all WinMTR.exe`，確認 RFC 3161 timestamp、subject、
  chain 與檔案 SHA-256；把驗證結果保存到 release record。
- 簽章完成後重新產生 `SHA256SUMS.txt`；不得使用簽章前的 hash。

## Windows 實機／VM gate

每個正式版本需保留 Windows 7 SP1、Windows 10、Windows 11 x64 的測試日期、
映像版本、commit、執行人與結果。每個系統至少驗證：

- 一般使用者、無 UAC 提升啟動；IPv4／IPv6 loopback 與一個已獲同意的遠端目標。
- start／stop／reset、有限 cycles + grace、DNS 解析、網路變更刷新。
- 96／120／144／192 DPI、跨螢幕移動、800×600 到 4K、高對比。
- 長 hostname／ISP、unknown folding、ECMP 展開、終點不重複、無多餘 scrollbar。
- TXT／HTML／CSV／JSON、clipboard、screenshot，以及唯讀／磁碟空間不足錯誤。

沒有 Authenticode 憑證或上述 VM 記錄時，只能標示為 CI build，不得標示為正式 release。
