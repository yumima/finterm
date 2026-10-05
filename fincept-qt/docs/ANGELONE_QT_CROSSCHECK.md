# AngelOne Cross-Check (OpenAlgo vs finterm)

Generated: 2026-03-26
Updated After Fixes: 2026-03-26
Final Cleanup: 2026-03-26
Baseline checklist: `C:\projects\openalgo\docs\audit\ANGEL_ONE_IMPLEMENTATION_USAGE_TRACE.md`
Target repo: `fincept-qt`

## Overall Snapshot

- Baseline OpenAlgo broker count: **31** (from baseline file)
- finterm registered brokers: **16**
  - Evidence: `fincept-qt\src\trading\BrokerRegistry.cpp:108-127`, `fincept-qt\src\trading\BrokerRegistry.cpp:129`
- AngelOne status in finterm: **Integrated for auth, orders, funds/positions, quotes/history, margins, instrument pipeline, and broker WS runtime path**

## Category Cross-Check (against baseline sections)

### 1) Plugin + core implementation
Status: **Completed**

- AngelOne broker class/profile:
  - `fincept-qt\src\trading\brokers\angelone\AngelOneBroker.h:6`
  - `fincept-qt\src\trading\brokers\angelone\AngelOneBroker.h:15`
- Broker registry wiring:
  - `fincept-qt\src\trading\BrokerRegistry.cpp:110`
- Broker ID mapping:
  - `fincept-qt\src\trading\TradingTypes.h:413-414`, `450-451`
- Core endpoints implemented:
  - Login: `...\AngelOneBroker.cpp:289`
  - Place/modify/cancel: `...\AngelOneBroker.cpp:347`, `376`, `395`
  - Orders/trades/positions/holdings/funds: `...\AngelOneBroker.cpp:422`, `460`, `476`, `514`, `552`
  - Quotes/history: `...\AngelOneBroker.cpp:610`, `705`

### 2) Auth/login flow usage
Status: **Completed**

- Angel-specific credential fields + packing:
  - `fincept-qt\src\screens\equity_trading\EquityCredentials.h:49`
  - `fincept-qt\src\screens\equity_trading\EquityCredentials.cpp:214-223`
  - `fincept-qt\src\screens\equity_trading\EquityCredentials.cpp:274-279`
- Login + save flow:
  - `fincept-qt\src\screens\equity_trading\EquityTradingScreen.cpp:976`
  - `fincept-qt\src\screens\equity_trading\EquityTradingScreen.cpp:993`

### 3) Runtime API dispatch (Angel selected via broker id)
Status: **Completed**

- Quote/history/positions/holdings/orders/funds dispatch from screen:
  - `...\EquityTradingScreen.cpp:406`, `475`, `503`, `529`, `555`, `581`, `608`
- Live order/cancel dispatch through UnifiedTrading:
  - `fincept-qt\src\trading\UnifiedTrading.cpp:121`, `131`, `151`, `156`

### 4) WebSocket integration
Status: **Completed (runtime path added)**

- ExchangeService now switches to broker bridge for broker stream IDs (includes AngelOne):
  - `fincept-qt\src\trading\ExchangeService.cpp:526`
- Broker bridge script reference added in runtime:
  - `fincept-qt\src\trading\ExchangeService.cpp:526-528`
- Broker bridge tick format (`type: "tick"`) supported in parser:
  - `fincept-qt\src\trading\ExchangeService.cpp:651`

### 5) Frontend/config usage equivalent
Status: **Completed for QT app scope**

- AngelOne data mapping template:
  - `fincept-qt\src\screens\data_mapping\DataMappingScreen.cpp:315-321`
- AngelOne instrument cache warm-load on app startup (improves symbol search/completer behavior):
  - `fincept-qt\src\app\main.cpp:67`

### 6) Instrument refresh pipeline parity
Status: **Completed**

- AngelOne download support added:
  - `fincept-qt\src\trading\instruments\InstrumentService.h:106`
  - `fincept-qt\src\trading\instruments\InstrumentService.cpp:392`
- AngelOne parser added in service:
  - `fincept-qt\src\trading\instruments\InstrumentService.cpp:81`
- Refresh routing now supports `angelone`:
  - `fincept-qt\src\trading\instruments\InstrumentService.cpp:328`
  - `fincept-qt\src\trading\instruments\InstrumentService.cpp:347`

### 7) Margin API parity
Status: **Completed**

- AngelOne broker now overrides both margin APIs:
  - `fincept-qt\src\trading\brokers\angelone\AngelOneBroker.h:52-55`
- Implementation and endpoint wiring:
  - `fincept-qt\src\trading\brokers\angelone\AngelOneBroker.cpp:801`
  - `fincept-qt\src\trading\brokers\angelone\AngelOneBroker.cpp:830`
  - `fincept-qt\src\trading\brokers\angelone\AngelOneBroker.cpp:821`, `856`

## Remaining items

- **None (for AngelOne integration scope).**
- Duplicate legacy files were removed:
  - `fincept-qt\src\trading\brokers\AngelOneBroker.cpp`
  - `fincept-qt\src\trading\brokers\AngelOneBroker.h`
- Active compiled source remains lowercase path in CMake:
  - `fincept-qt\CMakeLists.txt:219`, `814`

## Completion Verdict

- Core auth + order + portfolio + quotes/history: **Completed**
- Instrument refresh parity: **Completed**
- Margin calculator parity: **Completed**
- Broker websocket runtime parity: **Completed**
- QT frontend mapping/startup integration needed for AngelOne: **Completed**
- Optional code hygiene cleanup (duplicate legacy files): **Completed**
