# RTLAirLink

Driver Wi-Fi, AWDL/AirDrop native (~59.2 MB/s wire 2 chiều) và iPhone Mirroring mượt mà cho Realtek RTL8852BE trên macOS Tahoe (x86_64).

**Nghiên cứu, thiết kế và triển khai AWDL native bởi Anh Tài (xnpty) trong dự án Owly/RTLAirLink.** [Ghi công và lịch sử mã nguồn](CREDITS.md)

[![Ủng hộ qua Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/taingo) · [Discord](https://discord.com/users/856029147750858793) · [English](README.md)

## Danh sách card Realtek PCIe được hỗ trợ

RTLAirLink tích hợp bảng PCI ID và firmware `rtw89` tương tự `AirPort_RTW89`. **Hiện chỉ có `RTL8852BE` đã được kiểm thử thực tế trên phần cứng.** Tất cả các card khác đều ở trạng thái **Untested (chưa kiểm thử)** và **AirDrop / AWDL có thể không hoạt động** do luồng AWDL native (CAM/BlockAck/radio) hiện đang tối ưu riêng cho `RTL8852B`.

| Mẫu card | PCI ID (`10ec:xxxx`) | Chuẩn Wi-Fi | Trạng thái | AirDrop / AWDL |
| --- | --- | --- | --- | --- |
| **RTL8852BE** | `10ec:b852`, `10ec:b85b` | Wi-Fi 6 (802.11ax, 2×2, 80 MHz) | **Đã kiểm thử (Verified)** | **Hoạt động** (~59.2 MB/s wire 2 chiều, iPhone Mirroring) |
| **RTL8851BE** | `10ec:b851` | Wi-Fi 6 (802.11ax, 1×1, 80 MHz) | **Untested (Chưa kiểm thử)** | **Chưa kiểm thử — AirDrop có thể không hoạt động** |
| **RTL8852AE** | `10ec:8852`, `10ec:a85a` | Wi-Fi 6 (802.11ax, 2×2, 80 MHz) | **Untested (Chưa kiểm thử)** | **Chưa kiểm thử — AirDrop có thể không hoạt động** |
| **RTL8852BTE** | `10ec:b520` | Wi-Fi 6 (802.11ax, 2×2, 80 MHz) | **Untested (Chưa kiểm thử)** | **Chưa kiểm thử — AirDrop có thể không hoạt động** |
| **RTL8852CE** | `10ec:c852` | Wi-Fi 6E (802.11ax, 2×2, 160 MHz) | **Untested (Chưa kiểm thử)** | **Chưa kiểm thử — AirDrop có thể không hoạt động** |
| **RTL8922AE** | `10ec:8922`, `10ec:892b` | Wi-Fi 7 (802.11be, 2×2, 160 MHz) | **Untested (Chưa kiểm thử)** | **Chưa kiểm thử — AirDrop có thể không hoạt động** |
| **RTL8922DE** | `10ec:892d`, `10ec:882d`, `10ec:895d` | Wi-Fi 7 (802.11be, 2×2, 160 MHz) | **Untested (Chưa kiểm thử)** | **Chưa kiểm thử — AirDrop có thể không hoạt động** |

## Yêu cầu

- macOS Tahoe 26 trên x86_64 (Intel hoặc AMD) và một EFI OpenCore đang khởi động được.
- Python 3.9 trở lên để chạy trình cài tự động.
- AirDrop cần Bluetooth để dò thiết bị. Gói phát hành đã đóng gói sẵn `BlueToolFixup.kext` và `RealtekBluetoothFirmware.kext` cho Bluetooth USB Realtek tương thích (dùng cờ `--with-bluetooth`).

## Cài đặt v0.4.0

### Cài nhanh bằng 1 lệnh Terminal (khuyên dùng)

~~~bash
curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/0.4.0/install-rtlairlink.sh | bash
~~~

Nếu muốn cài hoặc thay luôn bộ kext Bluetooth Realtek USB (`BlueToolFixup.kext` + `RealtekBluetoothFirmware.kext` + `-btlfxallowanyaddr` + NVRAM Bluetooth):

~~~bash
curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/0.4.0/install-rtlairlink.sh | bash -s -- --with-bluetooth
~~~

### Cài offline / bằng file ZIP

1. Tải và giải nén [RTLAirLink-v0.4.0-x86_64.zip](https://github.com/xnpty/RTLAirLink/releases/download/0.4.0/RTLAirLink-v0.4.0-x86_64.zip).
2. Mở `Install-RTLAirLink.command`, kiểm tra đúng EFI trong bản xem trước rồi gõ `y` để sao lưu và cài.
3. Khởi động lại macOS.

**Lệnh cài v0.4.0 tự động làm những gì?**
- **Đóng gói sẵn toàn bộ kext cần thiết đã kiểm chứng trên Tahoe** (`Lilu.kext`, `RTLAirBridge.kext`, `IOSkywalkFamily.kext`, `IO80211FamilyLegacy.kext`, `AMFIPass.kext`, `RTLAirLink.kext` + `RTLAirLinkFront.kext`, kèm kext Bluetooth tùy chọn và `ocvalidate`) — không cần tải thêm từ ngoài.
- **Tự cài mới hoặc thay thế** các kext Wi-Fi cũ/chưa patch trên EFI sau khi sao lưu đầy đủ về `~/Downloads/RTLAirLink-Backups/`.
- **Tự chỉnh `config.plist`**: sắp xếp đúng thứ tự nạp trong `Kernel → Add`, chặn `com.apple.iokit.IOSkywalkFamily` (`Exclude`, `MinKernel 23.0.0`), đặt `SecureBootModel = Disabled`, thêm `boot-args` cần thiết (`-amfipassbeta`, thêm `-btlfxallowanyaddr` nếu bật `--with-bluetooth`) và kiểm tra hợp lệ bằng `ocvalidate`.
- **Tùy chọn cài `rtlairlink-rtgd`** (dịch vụ chạy nền tự động kích hoạt RTG 16/16 slot giúp iPhone Mirroring xem video không bị khựng/tụt FPS) và `rtlairlinkctl`.

[Hướng dẫn cài đặt chi tiết và cài thủ công](docs/INSTALL.md)

## Có gì mới và đã sửa ở v0.4.0?

- **AirDrop ~59.2 MB/s wire đối xứng 2 chiều (Mac ↔ iPhone):** Sửa lỗi race lock timestamp làm xóa peer giữa chừng, nâng hàng đợi TX lên 4096 gói + thu hồi TXBD chủ động + TX status không khóa + ghép khung 3-subframe A-MSDU hai chiều trên phần cứng A-MPDU 64 MPDU. Cả hai chiều đạt **59.2 MB/s wire (~49–50 MB/s thực tế)**, `0` TX refused / `0` TX expired.
- **iPhone Mirroring mượt không tụt FPS (RTG Peer Cache + `rtlairlink-rtgd`):** Bổ sung `APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE (130)`, `APPLE80211_IOC_PEER_CACHE_CONTROL (143)` và đẩy nhanh sự kiện `0x33` trong `RTLAirLinkFront`, kết hợp daemon nhẹ `rtlairlink-rtgd` tự bật chế độ RTG (`0x8e87` → `0xffff`, đủ 16/16 slot) khi mở iPhone Mirroring và tự tắt khi đóng. *(Để kích hoạt iPhone Mirroring trên Hackintosh, cài thêm [Mirra](https://github.com/eiyuumiru/Mirra) — ghi công chủ dự án **eiyuumiru** và **Dino-VN**.)*
- **Sửa lỗi sóng 5 GHz, roaming kênh & độ ổn định:** Sửa giới hạn công suất phát 5 GHz vùng VN, sửa lỗi bật AWDL độc lập trên 5 GHz và race `SET 120` `ENXIO`, chặn nhảy kênh `149 ↔ 6` và quét nền khi đang gửi file, sửa lỗi CAM Port 0 che mất RX BlockAck và tắt HW RTS bị lỗi trên AWDL.
- **Gói cài tất-cả-trong-một + lệnh cài 1 dòng:** Gộp sẵn toàn bộ kext cần thiết đã chạy ổn định trên Tahoe vào file tải về.

[Ghi chú phát hành đầy đủ](docs/RELEASE_NOTES.md) · [Chi tiết đo đạc](docs/STATUS.md)

## Kiểm tra & Khắc phục sự cố

- **Chạy thử xem trước (không ghi EFI):**
  ~~~bash
  curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/0.4.0/install-rtlairlink.sh | bash -s -- --efi /Volumes/EFI --dry-run
  ~~~
- **Kiểm tra kext đã nạp sau khi khởi động lại:**
  ~~~bash
  kmutil showloaded --list-only | grep -Ei 'Lilu|IOSkywalkFamily|IO80211FamilyLegacy|AMFIPass|io\.github\.xnpty\.RTLAirLink|BlueToolFixup|RealtekBluetoothFirmware'
  ~~~
- **Kiểm tra dịch vụ iPhone Mirroring RTG:** `/usr/local/libexec/rtlairlink-rtgd --status`
- **Khôi phục EFI:** Bản sao lưu `config.plist` và mọi kext bị thay luôn nằm tại `~/Downloads/RTLAirLink-Backups/`. [Hướng dẫn khôi phục](docs/INSTALL.md#recover-the-efi) · [Báo lỗi trên GitHub](https://github.com/xnpty/RTLAirLink/issues/new)

[Build từ mã nguồn](docs/BUILD.md) · [Giấy phép (GPLv2)](LICENSE) · [Ghi công](CREDITS.md)
