# RTLAirLink 0.3.0 verification

The v0.3.0 cohort was verified on hardware with a Realtek RTL8852BE running macOS Tahoe 25G241 and OpenCore 1.0.9.

- **32 MiB AirDrop (`AirDrop-32MiB.zip` over `awdl0`, `transport: "awdl"`):**
  - **Mac → iPhone:** **26.66 MB/s** (`26,661,752 B/s`, ~1.26 s).
  - **iPhone → Mac:** **24.97 MB/s** (`24,972,657 B/s`, ~1.34 s, `RX Reorder Timeouts = 0`).
- **182 MB AirDrop (`182,063,351` bytes over `awdl0`, `transport: "awdl"`):**
  - **Mac → iPhone:** **50.77 MB/s** (`50,773,724 B/s`, `3.592 s`, `130,094` packets sent, `99.97%` hardware radio ACK rate, `out-of-order bytes = 0`, `RTW89 TX Dropped = 0`).
  - **iPhone → Mac:** **14.21–16.95 MB/s** (`0` dropped RX frames across `79,443` packets, `RX Reorder Timeouts = 0`, `retransmitted bytes = 0`).

All release verification gates (`check_block_ack.py`, `check_native.py` with 9,818 checks, `check_raw_tx.py` with 66 checks, `check_ht.py`, `check_unicast.py`, `check_radio_timing.py`, `check_p2p_abi.py`, `make hosttest`, and `Bridge-Source` sanitizer tests) pass in both normal and ASan/UBSan modes.
