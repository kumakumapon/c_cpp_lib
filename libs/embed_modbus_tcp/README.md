# embed_modbus_tcp

ソケットに依存しないModbus TCPフレーミング層です。MBAPのProtocol ID=0、Length=2〜254、
Transaction IDとUnit IDを検証・保持し、既存 `modbus_slave_process_pdu` を再利用します。
通信の接続・送受信・タイムアウト・認証はアプリケーションが担当します。

- `embed_mbap_encode/decode`: 7バイトのヘッダ。decodeは完全なペイロードの到着を要求しません。
- `embed_modbus_tcp_handle`: 完全なADUを1つ処理。宛先不一致・不正長は0。
  副作用を起こすPDU処理の前に、出力容量260バイト以上を要求します。
- `embed_modbus_tcp_feed`: ソケットごとにゼロ初期化した `embed_modbus_tcp_connection` を保持。
  分割受信・連結ADUを処理し、応答をsendコールバックへ通知します。
  応答配列はコールバック中のみ有効です。非同期送信ならコピーしてください。
  不正MBAPではfailedを保持してfalseを返します。接続を切り、状態を初期化して再接続します。
- `embed_modbus_transaction_begin/end`: ゼロ初期化した管理構造体で最大16件を同時追跡。
  16bitの周回時も未完了IDと衝突しません。応答・タイムアウト・切断時にendしてください。
- `embed_modbus_rtu_tcp_handle`: CRC付きRTU ADUをそのまま処理。出力容量256バイト以上。
  RTU-over-TCPにはMBAP長がないため、呼び出し側で完全なフレーム境界を決定します。

独立したconnectionを用意すれば複数接続を処理できます。同一slaveの共有レジスタへの
並行アクセスはアプリケーションで直列化してください。コールバックから同一connectionを再入しないでください。

bridgeでは `bridge_router_bind_modbus` の後に `bridge_router_init_modbus_slave` を呼ぶと、
FC03/06/16がルーターと同じ保持レジスタへ接続されます。router・配列はslaveより長生きする必要があります。
